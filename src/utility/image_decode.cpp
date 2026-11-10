//
// Copyright 2024-2025 Andon "Kaldaien" Coleman
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

// Decoding images: what used to be LoadLibraryTexture in tabs/viewer.cpp, see utility/image_decode.h

#include <utility/image_decode.h>

#include <filesystem>
#include <SKIV.h>
#include <utility/utility.h>
#include <utility/skif_imgui.h>
#include <utility/fsutil.h>
#include <utility/registry.h>
#include <utility/sk_utility.h>

#include "DirectXTex/DirectXTex.h"
#include <wincodec.h>
#include <dxgi1_4.h> // IDXGIAdapter3::QueryVideoMemoryInfo (animated images)

#ifdef OpenEXR
#ifdef _M_X64
#include <package_misc/DirectXTexEXR.h>
#endif
#endif

#include <string>
#include <fstream>
#include <concurrent_queue.h>

#define SKIV_DEFAULT_GENERAL_PURPOSE_DECODER ImageDecoder_WIC
//#define SKIV_DEFAULT_GENERAL_PURPOSE_DECODER ImageDecoder_stbi
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_ONLY_PSD
#define STBI_ONLY_GIF
#define STBI_ONLY_HDR
//#define STBI_ONLY_PIC
//#define STBI_ONLY_PNM

#ifdef _M_X64
#include <jxl/codestream_header.h>
#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#include <jxl/resizable_parallel_runner.h>
#include <jxl/resizable_parallel_runner_cxx.h>
#include <jxl/types.h>
#endif

#include <stb_image.h>

// Animated images (APNG, GIF, WebP; AVIF: further below), and WebP still images: libwebp, and the decoders ported from SKIF. They use
//   stb_image's internals, so they are included here, where stb_image is implemented.
#include <functional>
#include <libwebp/src/webp/decode.h>
#include <libwebp/src/webp/demux.h>
#include "../utility/animated_images.inl"
#include "../utility/color_source.inl"

// Color conversion of images that are not sRGB (wide gamut): Little CMS (packages_misc\lcms2)
#include <thread>
#define CMS_NO_REGISTER_KEYWORD 1 // C++17 has no register keyword
#include <lcms2/include/lcms2.h>
#include "../utility/color_convert.inl"

#include <utility/image.h>

thread_local stbi__context::iccp_s SKIV_STBI_ICCP;
thread_local bool                  SKIV_STBI_srgb;
thread_local stbi__context::cicp_s SKIV_STBI_CICP;
thread_local stbi__context::sbit_s SKIV_STBI_SBIT;
thread_local stbi__result_info     SKIV_STBI_ResultInfo;

// Identify file type by reading the file signature
const std::initializer_list<FileSignature> supported_formats =
{
  FileSignature { L"image/jpeg",                { L".jpg", L".jpeg" },  { 0xFF, 0xD8, 0x00, 0x00 },   // JPEG (SOI; Start of Image)
                                                                        { 0xFF, 0xFF, 0x00, 0x00 } }, // JPEG App Markers are masked as they can be all over the place (e.g. 0xFF 0xD8 0xFF 0xED)
  FileSignature { L"image/png",                 { L".png"  },           { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A } },
  FileSignature { L"image/webp",                { L".webp" },           { 0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50 },   // 52 49 46 46 ?? ?? ?? ?? 57 45 42 50
                                                                        { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF } }, // mask
  FileSignature { L"image/bmp",                 { L".bmp"  },           { 0x42, 0x4D } },
  FileSignature { L"image/vnd.ms-photo",        { L".jxr", L".hdp"  },  { 0x49, 0x49, 0xBC } },
  FileSignature { L"image/vnd.adobe.photoshop", { L".psd"  },           { 0x38, 0x42, 0x50, 0x53 } },
  FileSignature { L"image/tiff",                { L".tiff", L".tif" },  { 0x49, 0x49, 0x2A, 0x00 } }, // TIFF: little-endian
  FileSignature { L"image/tiff",                { L".tiff", L".tif" },  { 0x4D, 0x4D, 0x00, 0x2A } }, // TIFF: big-endian
  FileSignature { L"image/gif",                 { L".gif"  },           { 0x47, 0x49, 0x46, 0x38, 0x37, 0x61 } }, // GIF87a
  FileSignature { L"image/gif",                 { L".gif"  },           { 0x47, 0x49, 0x46, 0x38, 0x39, 0x61 } }, // GIF89a
  FileSignature { L"image/vnd.radiance",        { L".hdr"  },           { 0x23, 0x3F, 0x52, 0x41, 0x44, 0x49, 0x41, 0x4E, 0x43, 0x45, 0x0A } }, // Radiance High Dynamic Range image file
#ifdef _M_X64
  FileSignature { L"image/x-exr",               { L".exr"  },           { 0x76, 0x2F, 0x31, 0x01 } },
#endif
  FileSignature { L"image/heic",                { L".heic" },           { 0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x68, 0x65, 0x69, 0x63 },   // ftypheic
                                                                        { 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } }, // ?? ?? ?? ?? 66 74 79 70 68 65 69 63
  FileSignature { L"image/avif",                { L".avif" },           { 0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x61, 0x76, 0x69, 0x66 },   // ftypavif
                                                                        { 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } }, // ?? ?? ?? ?? 66 74 79 70 61 76 69 66
  FileSignature { L"image/avif",                { L".avif", L".avifs" }, { 0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x61, 0x76, 0x69, 0x73 },  // ftypavis: image sequences (animated)
                                                                        { 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } }, // ?? ?? ?? ?? 66 74 79 70 61 76 69 73
  FileSignature { L"image/jxl",                 { L".jxl"  },           { 0xFF, 0x0A } },                                                             // Naked
  FileSignature { L"image/jxl",                 { L".jxl"  },           { 0x00, 0x00, 0x00, 0x0C, 0x4A, 0x58, 0x4C, 0x20, 0x0D, 0x0A, 0x87, 0x0A } }, // ISOBMFF-based container
  FileSignature { L"image/vnd-ms.dds",          { L".dds"  },           { 0x44, 0x44, 0x53, 0x20 } },
//FileSignature { L"image/x-targa",             { L".tga"  },           { 0x00, } }, // TGA has no real unique header identifier, so just use the file extension on those
};

bool isJXLDecoderAvailable (void)
{
  static HMODULE hModBrotliCommon = nullptr;
  static HMODULE hModBrotliDec    = nullptr;
  static HMODULE hModBrotliEnc    = nullptr;
  static HMODULE hModJXL          = nullptr;
  static HMODULE hModJXLCMS       = nullptr;
  static HMODULE hModJXLThreads   = nullptr;

  static const wchar_t* wszPluginArch =
    SK_RunLHIfBitness ( 64, LR"(x64\)",
                            LR"(x86\)" );

  SK_RunOnce (
  {
    SKIF_RegistrySettings& _registry =
      SKIF_RegistrySettings::GetInstance ();

    std::wstring path_to_sk =
      _registry.regKVPathSpecialK.getData ();

    std::error_code                            ec;
    if (! std::filesystem::exists (path_to_sk, ec))
    {
      path_to_sk =
        SKIF_CommonPathsCache::GetInstance ().my_documents.path;

      path_to_sk += LR"(\My Mods\SpecialK\)";
    }

    if (std::filesystem::exists (path_to_sk, ec))
    {
      path_to_sk += LR"(\PlugIns\ThirdParty\Image Codecs\libjxl\)";
      path_to_sk += wszPluginArch;

      std::filesystem::create_directories
                                  (path_to_sk, ec);
      if (std::filesystem::exists (path_to_sk, ec))
      {
        std::wstring path_to_brotlicommon = path_to_sk + L"brotlicommon.dll";
        std::wstring path_to_brotlienc    = path_to_sk + L"brotlienc.dll";
        std::wstring path_to_brotlidec    = path_to_sk + L"brotlidec.dll";
        std::wstring path_to_jxl_threads  = path_to_sk + L"jxl_threads.dll";
        std::wstring path_to_jxl_cms      = path_to_sk + L"jxl_cms.dll";
        std::wstring path_to_jxl          = path_to_sk + L"jxl.dll";

        if (! std::filesystem::exists (path_to_brotlicommon, ec))
          SKIF_Util_GetWebResource (L"https://sk-data.special-k.info/addon/ImageCodecs/libjxl/x64/brotlicommon.dll", path_to_brotlicommon);

        if (! std::filesystem::exists (path_to_brotlienc, ec))
          SKIF_Util_GetWebResource (L"https://sk-data.special-k.info/addon/ImageCodecs/libjxl/x64/brotlienc.dll",    path_to_brotlienc);

        if (! std::filesystem::exists (path_to_brotlidec, ec))
          SKIF_Util_GetWebResource (L"https://sk-data.special-k.info/addon/ImageCodecs/libjxl/x64/brotlidec.dll",    path_to_brotlidec);

        if (! std::filesystem::exists (path_to_jxl, ec))
          SKIF_Util_GetWebResource (L"https://sk-data.special-k.info/addon/ImageCodecs/libjxl/x64/jxl.dll",          path_to_jxl);

        if (! std::filesystem::exists (path_to_jxl_cms, ec))
          SKIF_Util_GetWebResource (L"https://sk-data.special-k.info/addon/ImageCodecs/libjxl/x64/jxl_cms.dll",      path_to_jxl_cms);

        if (! std::filesystem::exists (path_to_jxl_threads, ec))
          SKIF_Util_GetWebResource (L"https://sk-data.special-k.info/addon/ImageCodecs/libjxl/x64/jxl_threads.dll",  path_to_jxl_threads);

        // JXL depends on CMS to be loaded first

        hModBrotliCommon = LoadLibraryW (path_to_brotlicommon.c_str ());
        hModBrotliDec    = LoadLibraryW (path_to_brotlidec.   c_str ());
        hModBrotliEnc    = LoadLibraryW (path_to_brotlienc.   c_str ());
        hModJXLThreads   = LoadLibraryW (path_to_jxl_threads. c_str ());
        hModJXLCMS       = LoadLibraryW (path_to_jxl_cms.     c_str ());
        hModJXL          = LoadLibraryW (path_to_jxl.         c_str ());

        if ( hModBrotliCommon != nullptr &&
             hModBrotliDec    != nullptr &&
             hModBrotliEnc    != nullptr &&
             hModJXL          != nullptr &&
             hModJXLCMS       != nullptr &&
             hModJXLThreads   != nullptr )
        {
          PLOG_INFO << "Loaded JPEG XL DLLs from: " << path_to_sk;
          return true;
        }
      }
    }

    if (hModBrotliCommon == nullptr) hModBrotliCommon = LoadLibraryW (L"brotlicommon.dll");
    if (hModBrotliDec    == nullptr) hModBrotliDec    = LoadLibraryW (L"brotlidec.dll");
    if (hModBrotliEnc    == nullptr) hModBrotliEnc    = LoadLibraryW (L"brotlienc.dll");
    if (hModJXLThreads   == nullptr) hModJXLThreads   = LoadLibraryW (L"jxl_threads.dll");
    if (hModJXLCMS       == nullptr) hModJXLCMS       = LoadLibraryW (L"jxl_cms.dll");
    if (hModJXL          == nullptr) hModJXL          = LoadLibraryW (L"jxl.dll");

    if ( hModBrotliCommon != nullptr &&
         hModBrotliDec    != nullptr &&
         hModBrotliEnc    != nullptr &&
         hModJXL          != nullptr &&
         hModJXLCMS       != nullptr &&
         hModJXLThreads   != nullptr )
    {
      PLOG_INFO << "Loaded JPEG XL DLLs from default DLL search path";
      return true;
    }
  });

  const bool supported =
    ( hModBrotliCommon != nullptr &&
      hModBrotliDec    != nullptr &&
      hModBrotliEnc    != nullptr &&
      hModJXL          != nullptr &&
      hModJXLThreads   != nullptr &&
      hModJXLCMS       != nullptr );

  if (! supported)
  {
    ImGuiToast toast = {
      ImGuiToastType::Warning, 3333,
        "JPEG XL Unsupported because Special K is not Installed",
        "Please install Special K and run SKIV again to view JPEG XL images.\r\n\t"
        "> You may also manually place (64-bit versions of) jxl.dll, jxl_cms.dll and jxl_threads.dll in SKIV's directory."
    };
  }

  return supported;
}

bool isExtensionSupported (const std::wstring extension)
{
  for (auto& type : supported_formats)
  {
    if (SKIF_Util_HasFileExtension (extension, type))
    {
#ifdef _M_X64
      // Support for JXL is optional
      if (type.mime_type == L"image/jxl")
      {
        return isJXLDecoderAvailable ();
      }
#endif

      return true;
    }
  }

  return false;
}

#pragma region LoadTexture

extern CComPtr <ID3D11Device> SKIF_D3D11_GetDevice (bool bWait = true);

enum ImageDecoder {
  ImageDecoder_None,
  ImageDecoder_WIC,
  ImageDecoder_DDS,
  ImageDecoder_stbi,
  ImageDecoder_JXL,
#ifdef _M_X64
  ImageDecoder_EXR,
#endif
  ImageDecoder_HDR,
  ImageDecoder_UHDR,
  ImageDecoder_AVIF,
  ImageDecoder_WebP
};

class SK_AutoFile {
public:
  SK_AutoFile (FILE* pFile) : file_ (pFile)
  {
    if (file_ != nullptr)
    {
      auto orig_pos = ftell (pFile);
                      fseek (pFile,        0, SEEK_END);
              size_ = ftell (pFile);
                      fseek (pFile, orig_pos, SEEK_SET);
    }
  }

  ~SK_AutoFile (void)
  {
    if (file_ != nullptr)
    {
      fclose (std::exchange (file_, nullptr));
    }
  }

  size_t getInitialSize (void) const {
    return size_;
  }

private:
  size_t size_;
  FILE*  file_;
};

class SKIV_ScopedThreadPriority_Viewer
{
public:
  SKIV_ScopedThreadPriority_Viewer (int prio = THREAD_PRIORITY_TIME_CRITICAL) {
    orig_prio_ =
      GetThreadPriority (GetCurrentThread ());

    orig_process_class_ =
      GetPriorityClass (GetCurrentProcess ());

    SetPriorityClass  (GetCurrentProcess (), HIGH_PRIORITY_CLASS);
    SetThreadPriority (GetCurrentThread  (), prio);
  };

  ~SKIV_ScopedThreadPriority_Viewer (void) {
    SetPriorityClass  (GetCurrentProcess (), orig_process_class_);
    SetThreadPriority (GetCurrentThread  (), orig_prio_);
  }

private:
  int orig_prio_;
  int orig_process_class_;
};

int
SKIV_DXGI_NumberOfChannels (DXGI_FORMAT format)
{
  switch (format)
  {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM:
    case DXGI_FORMAT_B4G4R4A4_UNORM:
      return 4;

    case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
      return 3;

    case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R8G8_TYPELESS:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
      return 2;

    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM:
    case DXGI_FORMAT_R1_UNORM:
      return 1;

    case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
      return -1; // ?

    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
      return 4;

    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
      return 1;

    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
      return 2;

    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
      return 3;

    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
      return 4;

    case DXGI_FORMAT_AYUV:
    case DXGI_FORMAT_Y410:
    case DXGI_FORMAT_Y416:
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
    case DXGI_FORMAT_420_OPAQUE:
    case DXGI_FORMAT_YUY2:
    case DXGI_FORMAT_Y210:
    case DXGI_FORMAT_Y216:
    case DXGI_FORMAT_NV11:
    case DXGI_FORMAT_AI44:
    case DXGI_FORMAT_IA44:
    case DXGI_FORMAT_P8:
    case DXGI_FORMAT_A8P8:
      return -1; // ?

    case DXGI_FORMAT_P208:
    case DXGI_FORMAT_V208:
    case DXGI_FORMAT_V408:
      return -1; // ?

    default:
      return 0;
  }
}

#pragma region AnimatedImages

#pragma region AVIF

// Animated AVIF (an image sequence, by libavif: see isAVIFEncoderAvailable, which loads it). Not in animated_images.inl,
//   which is shared with SKIF and does not use libavif. Each frame is a whole image (no blending, nor disposal).

// Whether a file is an AVIF image sequence: its ftyp box has the brand avis (as its own, or among the compatible ones)
static bool
_IsAVIFSequence (const uint8_t* data, size_t size)
{
  if (size < 16 || memcmp (data + 4, "ftyp", 4) != 0)
    return false;

  const size_t box = std::min (static_cast <size_t> (_ReadBE32 (data)), size);

  for (size_t brand = 8; brand + 4 <= box; brand += 4)
  {
    if (brand == 12) // The minor version
      continue;

    if (memcmp (data + brand, "avis", 4) == 0)
      return true;
  }

  return false;
}

static bool
_IsAVIFSequenceFile (const std::wstring& path)
{
  uint8_t       head [256] = { };
  std::ifstream stream (std::filesystem::path (path), std::ios::binary);

  stream.read (reinterpret_cast <char*> (head), sizeof (head));
  return _IsAVIFSequence (head, static_cast <size_t> (stream.gcount ( )));
}

bool
SKIV_IsAnimatedFile (const std::wstring& path)
{
  return IsAnimatedImageFile (path) || _IsAVIFSequenceFile (path);
}

// The decoder (libavif's), set up for the file in data; nullptr if libavif is missing or cannot parse it
static avifDecoder*
_OpenAVIF (const uint8_t* data, size_t size)
{
  if (! isAVIFEncoderAvailable ())
    return nullptr;

  avifDecoder* decoder = SK_avifDecoderCreate ( );

  if (decoder == nullptr)
    return nullptr;

  decoder->maxThreads = static_cast <int> (std::clamp (std::thread::hardware_concurrency ( ), 1u, 64u));

  if (SK_avifDecoderSetIOMemory (decoder, data, size) != AVIF_RESULT_OK ||
      SK_avifDecoderParse       (decoder)             != AVIF_RESULT_OK || decoder->image == nullptr)
  {
    PLOG_ERROR << "libavif could not read the animated image: " << decoder->diag.error;
    SK_avifDecoderDestroy (decoder);
    return nullptr;
  }

  return decoder;
}

// The size of an AVIF image sequence, how many frames it has, and its colors (as the still AVIF decoder reads them). False
//   if it cannot be played: not a sequence, or HDR (PQ, HLG: the frames of animated images are 8 bpc sRGB), which is shown
//     as a still image then, by the AVIF decoder.
static bool
_ScanAVIF (const uint8_t* data, size_t size, uint32_t& width, uint32_t& height, uint32_t& count, color_source_s& color)
{
  avifDecoder* decoder = _OpenAVIF (data, size);

  if (decoder == nullptr)
    return false;

  const avifImage* image = decoder->image;

  width  = image->width;
  height = image->height;
  count  = static_cast <uint32_t> (std::max (decoder->imageCount, 0));

  color            = { };
  color.origin     = "AVIF";
  color.has_cicp   = (image->colorPrimaries          != AVIF_COLOR_PRIMARIES_UNSPECIFIED ||
                      image->transferCharacteristics != AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED);
  color.primaries  = static_cast <uint8_t> (image->colorPrimaries);
  color.transfer   = static_cast <uint8_t> (image->transferCharacteristics);
  color.matrix     = static_cast <uint8_t> (image->matrixCoefficients);
  color.full_range = (image->yuvRange == AVIF_RANGE_FULL) ? 1 : 0;

  _SetICC (color, image->icc.data, image->icc.size);

  const bool hdr = (_IsHDR10ColorSource (color) || color.transfer == AVIF_TRANSFER_CHARACTERISTICS_HLG);

  PLOG_INFO_IF (hdr) << "Animated AVIF is HDR (" << _DescribeColorSource (color) << "): showing its first frame";

  SK_avifDecoderDestroy (decoder);
  return ! hdr;
}

// Decodes the frames of an AVIF image sequence, as RGBA (8 bpc), handing each over to onFrame with how long it is shown
//   (in milliseconds), until it returns false. How many frames were decoded, or -1 if none could be.
static int
_DecodeAVIF (const uint8_t* data, size_t size, const std::function <bool (const uint8_t*, uint32_t)>& onFrame)
{
  avifDecoder* decoder = _OpenAVIF (data, size);

  if (decoder == nullptr)
    return -1;

  const uint32_t        width  = decoder->image->width,
                        height = decoder->image->height;
  std::vector <uint8_t> canvas (static_cast <size_t> (width) * height * 4);
  int                   frames = 0;

  while (SK_avifDecoderNextImage (decoder) == AVIF_RESULT_OK)
  {
    // All the frames of a sequence are its size
    if (decoder->image->width != width || decoder->image->height != height)
      break;

    avifRGBImage rgb;
    SK_avifRGBImageSetDefaults (&rgb, decoder->image);
    rgb.format      = AVIF_RGB_FORMAT_RGBA;
    rgb.depth       = 8;
    rgb.ignoreAlpha = AVIF_FALSE;
    rgb.maxThreads  = decoder->maxThreads;

    bool converted = false;

    if (SK_avifRGBImageAllocatePixels (&rgb) == AVIF_RESULT_OK)
    {
      converted = (SK_avifImageYUVToRGB (decoder->image, &rgb) == AVIF_RESULT_OK);

      if (converted)
        for (uint32_t y = 0; y < height; y++)
          memcpy (&canvas [static_cast <size_t> (y) * width * 4], rgb.pixels + static_cast <size_t> (y) * rgb.rowBytes, static_cast <size_t> (width) * 4);

      SK_avifRGBImageFreePixels (&rgb);
    }

    if (! converted)
      break;

    frames++;

    const uint32_t delay = static_cast <uint32_t> (std::lround (std::max (decoder->imageTiming.duration, 0.0) * 1000.0));

    if (! onFrame (canvas.data ( ), delay))
      break;
  }

  SK_avifDecoderDestroy (decoder);
  return (frames > 0) ? frames : -1;
}

#pragma endregion

// An animated image (APNG, GIF, WebP, AVIF), read and parsed: its canvas, and how many frames it has. Decode ( ) hands
//   over its frames (the canvas as it is at each, RGBA, and how long it is shown).
struct animated_image_s
{
  std::vector <uint8_t> file;
  apng_s                apng;    // APNG: the parsed frames
  bool                  isGIF  = false;
  bool                  isWebP = false;
  bool                  isAVIF = false;
  color_source_s        color;   // AVIF: its colors (those of the others are read from the file by _ReadColorSource)
  uint32_t              width  = 0,
                        height = 0,
                        count  = 0;

  int Decode (const std::function <bool (const uint8_t*, uint32_t)>& onFrame) const
  {
    return (isAVIF) ? _DecodeAVIF (file.data ( ), file.size ( ), onFrame) :
           (isWebP) ? _DecodeWebP (file.data ( ), file.size ( ), onFrame) :
           (isGIF)  ? _DecodeGIF  (file.data ( ), file.size ( ), onFrame)
                    : _DecodeAPNG (apng, onFrame);
  }
};

// All the frames of an animated image together are kept below this much video memory (they are scaled down to fit), and
//   there can be no more frames than this. Below a quarter of the size (each way) it would look too poor: it is shown as
//     a still image then.
#ifdef _WIN64
static constexpr double   s_animBudget       = 2048.0 * 1024 * 1024;
#else
static constexpr double   s_animBudget       =  512.0 * 1024 * 1024;
#endif
static constexpr uint32_t s_animMaxFrames    = 10000;
static constexpr double   s_animMinScale     = 0.25;
static constexpr size_t   s_animPageSize     = 4096; // The shared textures (pages) are at most this large (each way)
static constexpr size_t   s_animMaxCellSize  = 2048; // Frames larger than this (each way, with their border) get a texture each

// Reads and parses an animated image. False if it is not one (fewer than two frames), is malformed, or is too large.
static bool
_OpenAnimatedImage (const std::wstring& path, animated_image_s& image)
{
  std::error_code ec;
  const auto fileSize = std::filesystem::file_size (std::filesystem::path (path), ec);

  if (ec || fileSize < 16 || fileSize > 1024ULL * 1024 * 1024)
    return false;

  image.file.resize (static_cast <size_t> (fileSize));
  {
    std::ifstream stream (std::filesystem::path (path), std::ios::binary);

    if (! stream.read (reinterpret_cast <char*> (image.file.data ( )), image.file.size ( )))
      return false;
  }

  image.isAVIF = _IsAVIFSequence (image.file.data ( ), image.file.size ( ));
  image.isWebP = _IsWebP         (image.file.data ( ), image.file.size ( ));
  image.isGIF  = (memcmp (image.file.data ( ), "GIF8", 4) == 0);

  if (image.isAVIF)
  {
    if (! _ScanAVIF (image.file.data ( ), image.file.size ( ), image.width, image.height, image.count, image.color))
      return false;
  }

  else if (image.isWebP)
  {
    if (! _ScanWebP (image.file.data ( ), image.file.size ( ), image.width, image.height, image.count))
      return false;
  }

  else if (image.isGIF)
  {
    if (! _ScanGIF (image.file.data ( ), image.file.size ( ), image.width, image.height, image.count))
      return false;
  }

  else
  {
    if (! _ParseAPNG (image.file.data ( ), image.file.size ( ), image.apng))
      return false;

    image.width  = image.apng.width;
    image.height = image.apng.height;
    image.count  = static_cast <uint32_t> (image.apng.frames.size ( ));
  }

  if (image.count < 2 || image.count > s_animMaxFrames || image.width == 0 || image.height == 0 || image.width > 16384 || image.height > 16384)
  {
    PLOG_WARNING_IF (image.count > s_animMaxFrames) << "Animated image has too many frames to play (" << image.count << ")";
    return false;
  }

  return true;
}

// Creates a texture (and its view) of the image (RGBA), as sRGB: as the general purpose decoder (WIC) loads 8-bpc images
static bool
_CreateAnimationTexture (ID3D11Device* pDevice, const DirectX::ScratchImage& img, CComPtr <ID3D11ShaderResourceView>& srv)
{
  CComPtr <ID3D11Texture2D> pTex2D;

  if (FAILED (DirectX::CreateTextureEx (pDevice, img.GetImages ( ), img.GetImageCount ( ), img.GetMetadata ( ),
                                        D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, DirectX::CREATETEX_FORCE_SRGB,
                                        (ID3D11Resource **)&pTex2D.p)) || pTex2D.p == nullptr)
    return false;

  D3D11_SHADER_RESOURCE_VIEW_DESC
    srv_desc                           = { };
    srv_desc.Format                    = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels       = UINT_MAX;
    srv_desc.Texture2D.MostDetailedMip =  0;

  return SUCCEEDED (pDevice->CreateShaderResourceView (pTex2D.p, &srv_desc, &srv.p)) && srv.p != nullptr;
}

// Loads all the frames of an animated image (APNG, GIF, WebP) into image (anim, and pRawTexSRV: its first frame). Each
//   frame is the canvas as it is at that point, at its full size unless all of them would not fit s_animBudget.
//     wanted (if set) is asked between frames, and stops the load once it returns false. False if it is not animated,
//       cannot be decoded, is too large, or was stopped: it is then loaded as a still image.
static bool
_LoadAnimatedImage (image_s& image, const std::function <bool (void)>& wanted)
{
  DWORD pre = SKIF_Util_timeGetTime1 ( );

  animated_image_s source;

  if (! _OpenAnimatedImage (image.file_info.path, source))
    return false;

  // AVIF: its colors come from libavif (the other formats' were read from the file by _ReadColorSource)
  if (source.isAVIF)
    image.color_source = source.color;

  const uint32_t width  = source.width,
                 height = source.height,
                 count  = source.count;

  auto pDevice =
    SKIF_D3D11_GetDevice ();

  if (! pDevice)
    return false;

  // The budget: s_animBudget, or three quarters of the video memory Windows says is left for SKIV, if that is less
  double budget = s_animBudget;

  if (CComQIPtr <IDXGIDevice> pDXGIDevice (pDevice.p); pDXGIDevice.p != nullptr)
  {
    CComPtr <IDXGIAdapter> pAdapter;

    if (SUCCEEDED (pDXGIDevice->GetAdapter (&pAdapter.p)))
    {
      DXGI_QUERY_VIDEO_MEMORY_INFO info = { };

      if (CComQIPtr <IDXGIAdapter3> pAdapter3 (pAdapter.p);
          pAdapter3.p != nullptr && SUCCEEDED (pAdapter3->QueryVideoMemoryInfo (0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)) && info.Budget > 0)
      {
        const double available = (info.Budget > info.CurrentUsage) ? static_cast <double> (info.Budget - info.CurrentUsage) : 0.0;
        budget = std::min (budget, available * 0.75);
      }
    }
  }

  // Colors that are not sRGB (wide gamut, see color_convert.inl): frames of scRGB (half floats), which keeps the colors
  //   outside of sRGB, if they fit the budget at their full size (they take twice the memory); otherwise they are made
  //     sRGB (8 bpc), the colors outside of it clipped
  std::unique_ptr <void, decltype (&cmsCloseProfile)> sourceProfile (_CreateSourceProfile (image.color_source), &cmsCloseProfile);

  const bool wide =
    (sourceProfile != nullptr && static_cast <double> (width) * height * 8.0 * count <= budget);

  const size_t bpp = (wide) ? 8 : 4; // Bytes per pixel

  // The size the frames are made at: as they are, unless all of them would take more than the budget
  double scale = 1.0;

  if (const double bytes = static_cast <double> (width) * height * static_cast <double> (bpp) * count; bytes > budget)
  {
    scale = std::sqrt (budget / bytes);

    if (scale < s_animMinScale)
    {
      PLOG_WARNING << "Animated image is too large to play (" << width << "x" << height << ", " << count << " frames, "
                   << static_cast <int> (budget / (1024 * 1024)) << " MB of video memory to spare): showing its first frame";
      return false;
    }

    PLOG_INFO << "Animated image is scaled down to " << static_cast <int> (scale * 100.0) << "% to fit in " << static_cast <int> (budget / (1024 * 1024)) << " MB of video memory";
  }

  const size_t outWidth  = std::max (static_cast <size_t> (width  * scale), size_t (1)),
               outHeight = std::max (static_cast <size_t> (height * scale), size_t (1));
  const bool   resize    = (outWidth != width || outHeight != height);

  // The frames are made (and scaled, as browsers do) as plain RGBA, as DirectX::Resize cannot scale sRGB images with
  //   WIC's filters; their textures are sRGB (_CreateAnimationTexture). Wide gamut ones: scRGB, as half floats.
  const DXGI_FORMAT format = (wide) ? DXGI_FORMAT_R16G16B16A16_FLOAT
                                    : DXGI_FORMAT_R8G8B8A8_UNORM;

  // The pages: as many frames side by side as fit in s_animPageSize, each in a cell with a one-pixel border
  const size_t cellWidth  = outWidth  + 2,
               cellHeight = outHeight + 2;
  const bool   shared     = (cellWidth <= s_animMaxCellSize && cellHeight <= s_animMaxCellSize);
  const size_t cols       = (shared) ? std::clamp (s_animPageSize / cellWidth, size_t (1), static_cast <size_t> (count)) : 1,
               perPage    = (shared) ? cols * std::max (s_animPageSize / cellHeight, size_t (1))                       : 1;

  auto anim = std::make_shared <image_anim_s> ( );
  anim->resolution = ImVec2 (static_cast <float> (outWidth), static_cast <float> (outHeight));
  anim->frames   .reserve (count);
  anim->frameEnds.reserve (count);

  DirectX::ScratchImage page;            // The page being filled
  size_t                pageStart = 0,   // Its first frame
                        pageCols  = 0,
                        pageCount = 0;   // How many frames it is for
  const char*           failure   = nullptr; // Why it could not be loaded (logged)

  // Makes the page being filled a texture, which its frames share
  auto _FinishPage = [&](void) -> bool
  {
    CComPtr <ID3D11ShaderResourceView> srv;

    if (! _CreateAnimationTexture (pDevice, page, srv))
      return false;

    const DirectX::TexMetadata& meta = page.GetMetadata ( );

    for (size_t i = pageStart; i < anim->frames.size ( ); i++)
      anim->frames [i].srv = srv; // Its own reference

    anim->textures++;
    anim->bytes += page.GetPixelsSize ( );

    // The texture coordinates were set relative to the page's size
    for (size_t i = pageStart; i < anim->frames.size ( ); i++)
    {
      auto& frame = anim->frames [i];
      frame.uv0 = ImVec2 ( frame.offset.x                               / meta.width,  frame.offset.y                                / meta.height);
      frame.uv1 = ImVec2 ((frame.offset.x + static_cast <float> (outWidth)) / meta.width, (frame.offset.y + static_cast <float> (outHeight)) / meta.height);
    }

    page.Release ( );
    return true;
  };

  DWORD elapsed = 0;

  // Wide gamut frames that did not fit the budget as scRGB: made sRGB, the colors outside of it clipped
  std::unique_ptr <void, decltype (&cmsDeleteTransform)> toSRGB (nullptr, &cmsDeleteTransform);

  if (sourceProfile != nullptr && ! wide)
  {
    toSRGB.reset (_CreateSRGBTransform (sourceProfile.get ( )));

    PLOG_INFO_IF (toSRGB != nullptr) << "[Image Processing] The animated image's colors (" << _DescribeColorSource (image.color_source)
                                     << ") are converted to sRGB, clipped: as scRGB, its frames would not fit in video memory";
  }

  PLOG_INFO_IF (wide) << "[Image Processing] The animated image's colors (" << _DescribeColorSource (image.color_source)
                      << ") are converted to scRGB";

  auto _OnFrame = [&](const uint8_t* canvas, uint32_t delay) -> bool
  {
    if (wanted && ! wanted ( ))
    {
      failure = "it is no longer wanted";
      return false;
    }

    // More frames than it was scanned to have (its pages are laid out for those)
    if (anim->frames.size ( ) >= count)
      return false;

    // The canvas at this point, scaled if need be
    DirectX::ScratchImage  img;
    DirectX::ScratchImage  resized;
    DirectX::ScratchImage* pImg = &img;

    if (FAILED (img.Initialize2D (format, width, height, 1, 1)))
    {
      failure = "out of memory (canvas)";
      return false;
    }

    if (wide)
    {
      if (! _ConvertToScRGB (sourceProfile.get ( ), TYPE_RGBA_8, canvas, static_cast <size_t> (width) * 4,
                             img.GetImage (0, 0, 0)->pixels, img.GetImage (0, 0, 0)->rowPitch, width, height))
      {
        failure = "a frame's colors could not be converted";
        return false;
      }
    }

    else
      memcpy (img.GetImage (0, 0, 0)->pixels, canvas, static_cast <size_t> (width) * height * 4);

    if (toSRGB != nullptr)
      for (uint32_t y = 0; y < height; y++)
        cmsDoTransform (toSRGB.get (), img.GetImage (0, 0, 0)->pixels + y * img.GetImage (0, 0, 0)->rowPitch,
                                       img.GetImage (0, 0, 0)->pixels + y * img.GetImage (0, 0, 0)->rowPitch, width);

    if (resize)
    {
      if (FAILED (DirectX::Resize (img.GetImages ( ), img.GetImageCount ( ), img.GetMetadata ( ),
                                   outWidth, outHeight, DirectX::TEX_FILTER_FANT, resized)))
      {
        failure = "a frame could not be scaled";
        return false;
      }

      pImg = &resized;
    }

    const DirectX::Image* src = pImg->GetImage (0, 0, 0);

    image_anim_s::frame_s frame;

    if (! shared)
    {
      if (! _CreateAnimationTexture (pDevice, *pImg, frame.srv))
      {
        failure = "a frame's texture could not be created (out of video memory?)";
        return false;
      }

      anim->textures++;
      anim->bytes += pImg->GetPixelsSize ( );
    }

    else
    {
      const size_t index = anim->frames.size ( );

      // A new page: for the frames left, up to as many as fit
      if (page.GetImageCount ( ) == 0)
      {
        pageStart = index;
        pageCount = std::min (perPage, static_cast <size_t> (count) - index);
        pageCols  = std::min (cols,    pageCount);

        const size_t pageRows = (pageCount + pageCols - 1) / pageCols;

        if (FAILED (page.Initialize2D (format, pageCols * cellWidth, pageRows * cellHeight, 1, 1)))
        {
          failure = "out of memory (page)";
          return false;
        }

        memset (page.GetPixels ( ), 0, page.GetPixelsSize ( )); // The cells no frame uses (the last row)
      }

      // Into its cell, with the border repeating its edges
      const DirectX::Image* dst  = page.GetImage (0, 0, 0);
      const size_t          slot = index - pageStart,
                            x    = (slot % pageCols) * cellWidth,
                            y    = (slot / pageCols) * cellHeight;

      for (size_t row = 0; row < cellHeight; row++)
      {
        const size_t   srcRow = (row == 0) ? 0 : std::min (row - 1, outHeight - 1);
        const uint8_t* s      = src->pixels + srcRow * src->rowPitch;
        uint8_t*       d      = dst->pixels + (y + row) * dst->rowPitch + x * bpp;

        memcpy (d,                        s,                          bpp);
        memcpy (d + bpp,                  s,                          outWidth * bpp);
        memcpy (d + bpp + outWidth * bpp, s + (outWidth - 1) * bpp,   bpp);
      }

      frame.offset = ImVec2 (static_cast <float> (x + 1), static_cast <float> (y + 1));
    }

    elapsed += delay;

    anim->frames   .push_back (std::move (frame));
    anim->frameEnds.push_back (elapsed);

    // The page is full
    if (shared && anim->frames.size ( ) == pageStart + pageCount && ! _FinishPage ( ))
    {
      failure = "a page's texture could not be created (out of video memory?)";
      return false;
    }

    return true;
  };

  const int decoded = source.Decode (_OnFrame);

  // A GIF that broke off early: its page is not full
  if (failure == nullptr && shared && page.GetImageCount ( ) != 0 && ! _FinishPage ( ))
    failure = "a page's texture could not be created (out of video memory?)";

  if (failure == nullptr && (decoded < 2 || anim->frames.size ( ) < 2))
    failure = "it could not be decoded";

  if (failure != nullptr)
  {
    PLOG_WARNING << "Could not load the animated image, as " << failure << " (" << anim->frames.size ( ) << " of " << count << " frames): showing its first frame";
    return false; // Released right away: nothing else has seen them
  }

  anim->duration = elapsed;
  anim->wide     = wide;

  image.anim         = anim;
  image.pRawTexSRV   = anim->frames [0].srv; // A reference of its own
  image.uv0          = anim->frames [0].uv0;
  image.uv1          = anim->frames [0].uv1;
  image.width        = static_cast <float> (width);
  image.height       = static_cast <float> (height);
  image.bpc          = 8;
  image.channels     = 4;
  image.is_hdr       = false;
  image.is_wcg       = false; // Wide gamut frames (anim->wide) have no gamut coverage: they are parts of shared textures
  image.light_info   = { };
  image.colorimetry  = { };

  DWORD post = SKIF_Util_timeGetTime1 ( );
  PLOG_INFO << "[Image Processing] Loaded an animated image (" << anim->frames.size ( ) << " frames, " << outWidth << "x" << outHeight << ", "
            << anim->duration << " ms) into " << anim->textures << " texture(s), " << (anim->bytes / (1024 * 1024)) << " MB, in " << (post - pre) << " ms.";

  return true;
}

#pragma endregion

// The ICC profile or Exif color space WIC finds in a file (TIFF, HEIF, JPEG XR: formats color_source.inl does not read itself)
static void
_ReadColorSourceWIC (const std::wstring& path, color_source_s& out)
{
  bool                iswic2  = false;
  IWICImagingFactory* factory = DirectX::GetWICFactory (iswic2);

  CComPtr <IWICBitmapDecoder>     decoder;
  CComPtr <IWICBitmapFrameDecode> frame;
  UINT                            count = 0;

  if (factory == nullptr ||
      FAILED (factory->CreateDecoderFromFilename (path.c_str (), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder.p)) ||
      FAILED (decoder->GetFrame (0, &frame.p)) ||
      FAILED (frame->GetColorContexts (0, nullptr, &count)) || count == 0)
    return;

  std::vector <CComPtr <IWICColorContext>> contexts (count);
  std::vector <IWICColorContext*>          raw      (count);

  for (UINT i = 0; i < count; i++)
  {
    if (FAILED (factory->CreateColorContext (&contexts [i].p)))
      return;

    raw [i] = contexts [i].p;
  }

  if (FAILED (frame->GetColorContexts (count, raw.data (), &count)))
    return;

  out.origin = "WIC";

  for (UINT i = 0; i < count; i++)
  {
    WICColorContextType type = WICColorContextUninitialized;

    if (FAILED (raw [i]->GetType (&type)))
      continue;

    if (type == WICColorContextProfile && out.icc.empty ())
    {
      UINT bytes = 0;

      if (SUCCEEDED (raw [i]->GetProfileBytes (0, nullptr, &bytes)) && bytes > 0)
      {
        std::vector <BYTE> icc (bytes);

        if (SUCCEEDED (raw [i]->GetProfileBytes (bytes, icc.data (), &bytes)))
          _SetICC (out, icc.data (), bytes);
      }
    }

    else if (type == WICColorContextExifColorSpace)
    {
      UINT value = 0;

      if (SUCCEEDED (raw [i]->GetExifColorSpace (&value)))
        out.exif_color_space = static_cast <uint16_t> (value);
    }
  }
}

// How the file describes its colors: read from the file itself (AVIF and JPEG XL: by their decoders, below). Shown in the
//   details panel; nothing uses it to convert colors yet.
static void
_ReadColorSource (image_s& image, const FileSignature* image_sig, FILE* pImageFile, size_t fileSize)
{
  image.color_source = { };

  if (image_sig == nullptr || pImageFile == nullptr)
    return;

  const std::wstring& mime = image_sig->mime_type;

  if (mime == L"image/png" || mime == L"image/jpeg" || mime == L"image/webp")
  {
    // Their color information comes before the image data (but for the EXIF chunk of a WebP), so the start of the file will do
    std::vector <uint8_t> head (std::min <size_t> (fileSize, 64 * 1024 * 1024));

    fseek (pImageFile, 0, SEEK_SET);
    head.resize (fread (head.data (), 1, head.size (), pImageFile));
    rewind (pImageFile);

    if      (mime == L"image/png")  _ReadColorSourcePNG  (head.data (), head.size (), image.color_source);
    else if (mime == L"image/jpeg") _ReadColorSourceJPEG (head.data (), head.size (), image.color_source);
    else                            _ReadColorSourceWebP (head.data (), head.size (), image.color_source);
  }

  else if (mime == L"image/tiff" || mime == L"image/heif" || mime == L"image/heic" || mime == L"image/vnd.ms-photo")
    _ReadColorSourceWIC (image.file_info.path, image.color_source);

  else if (mime == L"image/x-exr")
    image.color_source.format_defined = "Linear, Rec. 709 (OpenEXR)";

  else if (mime == L"image/vnd.radiance")
    image.color_source.format_defined = "Linear (Radiance HDR)";

  else if (mime == L"image/vnd-ms.dds")
    image.color_source.format_defined = "Set by the texture format (DDS)";
}

static bool
_DecodeImage (image_s& image, const SKIV_DecodeRequest& request, std::vector <ImGuiToast>& notices)
{
  const std::function <bool (void)>& wanted = request.wanted;

  SKIV_ScopedThreadPriority_Viewer _scoped_thread_prio;


  CComPtr <ID3D11Texture2D> pRawTex2D;
  CComPtr <ID3D11Texture2D> pGamutCoverageTex2D;
  DirectX::TexMetadata        meta      = { };
  DirectX::ScratchImage        img      = { };
  DirectX::ScratchImage        img_srgb = { };

  bool succeeded = false;
  bool converted = false;

  DWORD pre = SKIF_Util_timeGetTime1 ();

  if (image.file_info.path.empty ())
    return false;

  const std::filesystem::path
    imagePath (image.file_info.path.data ());

  std::wstring ext   = SKIF_Util_ToLowerW (imagePath.extension ().wstring ());
  std::string szPath = SK_WideCharToUTF8  (image.file_info.path);

  ImageDecoder decoder = ImageDecoder_None;

  if (ext == L".tga")
    decoder = SKIV_DEFAULT_GENERAL_PURPOSE_DECODER;

        FILE*          pImageFile = nullptr;
  const FileSignature* image_sig  = nullptr;

  if (decoder == ImageDecoder_None)
  {
    static size_t
        maxLength  = 0;
    if (maxLength == 0)
    {
      for (auto& type : supported_formats)
        if (type.signature.size () > maxLength)
          maxLength = type.signature.size ();
    }

    pImageFile =
      _wfopen (imagePath.c_str (), L"rb");

    if (! pImageFile)
    {
      PLOG_ERROR << "Failed to open file!";
      return false;
    }

    else
    {
      std::vector <char>
              buffer         (maxLength);
      fread  (buffer.data (), maxLength, 1, pImageFile);
      rewind (                              pImageFile);

      for (auto& type : supported_formats)
      {
        if (SKIF_Util_HasFileSignature (buffer, type))
        {
          image_sig = &type;

          PLOG_INFO << "Detected an " << type.mime_type << " image";

          decoder =
             (type.mime_type == L"image/jpeg"                ) ?
                   (SKIV_Image_IsUltraHDR (imagePath.c_str ()) ? ImageDecoder_UHDR :
                                                                 SKIV_DEFAULT_GENERAL_PURPOSE_DECODER):
             (type.mime_type == L"image/png"                 ) ? ImageDecoder_stbi :
             //(type.mime_type == L"image/png"                 ) ? ImageDecoder_WIC  : // Use WIC for proper color correction and for decoding many PNG images that stbi cannot
             (type.mime_type == L"image/bmp"                 ) ? SKIV_DEFAULT_GENERAL_PURPOSE_DECODER :
             (type.mime_type == L"image/vnd.adobe.photoshop" ) ? ImageDecoder_stbi : // Consider gamma broken, since stbi doesn't handle it correctly
             (type.mime_type == L"image/gif"                 ) ? SKIV_DEFAULT_GENERAL_PURPOSE_DECODER :
             (type.mime_type == L"image/vnd.radiance"        ) ? ImageDecoder_HDR  :
           //(type.mime_type == L"image/x-targa"             ) ? ImageDecoder_stbi : // TGA has no real unique header identifier, so just use the file extension on those
             (type.mime_type == L"image/vnd.ms-photo"        ) ? ImageDecoder_WIC  :
             (type.mime_type == L"image/webp"                ) ? ImageDecoder_WebP : // libwebp (WIC needs the WebP Image Extensions)
             (type.mime_type == L"image/tiff"                ) ? ImageDecoder_WIC  :
             (type.mime_type == L"image/heif"                ) ? ImageDecoder_WIC  :
             (type.mime_type == L"image/heic"                ) ? ImageDecoder_WIC  :
             (type.mime_type == L"image/avif"                ) ? ImageDecoder_AVIF :
#if _WIN64
             (type.mime_type == L"image/jxl"                 ) ? ImageDecoder_JXL  :
#endif
             (type.mime_type == L"image/vnd-ms.dds"          ) ? ImageDecoder_DDS  :
#ifdef _M_X64
             (type.mime_type == L"image/x-exr"               ) ? ImageDecoder_EXR  :
#endif
                                                                 ImageDecoder_WIC;   // Not actually being used

          // None of this is technically correct other than the .hdr case,
          //   they can all be SDR or HDR.
          if (type.mime_type == L"image/vnd.radiance" || // .hdr
              type.mime_type == L"image/vnd.ms-photo" || // .jxr
              type.mime_type == L"image/avif"         || // .avif
              type.mime_type == L"image/x-exr")          // .exr
          {
            image.is_hdr = true;
          }

          break;
        }
      }
    }
  }

  SK_AutoFile _(pImageFile);

  _ReadColorSource (image, image_sig, pImageFile, _.getInitialSize ());

  // Animated images (APNG, GIF, WebP, AVIF): all their frames, which the viewer plays. One that cannot be played (too large,
  //   or malformed) is loaded as a still image below: its first frame (an APNG: its default image).
  //     GIFs skip IsAnimatedImageFile, which only reads GIFs up to 64 MB: _LoadAnimatedImage counts their frames itself.
  //       AVIF: image sequences (by their ftyp box), whose frames libavif decodes (HDR ones are shown as still images).
  if (request.animatedImages && image_sig != nullptr &&
     ((image_sig->mime_type == L"image/gif") ||
      (image_sig->mime_type == L"image/avif" && _IsAVIFSequenceFile (image.file_info.path)) ||
     ((image_sig->mime_type == L"image/png" ||
       image_sig->mime_type == L"image/webp") && IsAnimatedImageFile (image.file_info.path))))
  {
    if (_LoadAnimatedImage (image, wanted))
      return true;

    // Stopped, as the image is no longer wanted
    if (wanted && ! wanted ( ))
      return false;
  }

  auto _scratchMemory =
    std::make_unique <unsigned char []> (_.getInitialSize ());

  PLOG_ERROR_IF(decoder == ImageDecoder_None) << "Failed to detect file type!";
  PLOG_DEBUG_IF(decoder == ImageDecoder_stbi) << "Using stbi decoder...";
  PLOG_DEBUG_IF(decoder == ImageDecoder_WIC ) << "Using WIC decoder...";
  PLOG_DEBUG_IF(decoder == ImageDecoder_DDS ) << "Using DDS decoder...";
#ifdef _M_X64
  PLOG_DEBUG_IF(decoder == ImageDecoder_JXL ) << "Using JPEG XL decoder...";
  PLOG_DEBUG_IF(decoder == ImageDecoder_EXR ) << "Using OpenEXR decoder...";
#endif
  PLOG_DEBUG_IF(decoder == ImageDecoder_HDR ) << "Using Radiance HDR decoder...";
  PLOG_DEBUG_IF(decoder == ImageDecoder_UHDR) << "Using Ultra HDR decoder...";
  PLOG_DEBUG_IF(decoder == ImageDecoder_WebP) << "Using libwebp decoder...";

  if (decoder == ImageDecoder_None)
    return false;

  if (decoder == ImageDecoder_UHDR)
  {
    fseek  (pImageFile,                                 0, SEEK_SET  );
    fread  (_scratchMemory.get (), _.getInitialSize (), 1, pImageFile);
    rewind (pImageFile);

    image.light_info.isHDR = true;
    image.is_hdr           = true;

    SKIV_Image_LoadUltraHDR (img, _scratchMemory.get (), static_cast <int> (_.getInitialSize ()));

    meta           = img.GetMetadata ();
    meta.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

    converted = true;
    succeeded = true;
  }

  if (decoder == ImageDecoder_stbi)
  {
    // If desired_channels is non-zero, *channels_in_file has the number of components that _would_ have been
    // output otherwise. E.g. if you set desired_channels to 4, you will always get RGBA output, but you can
    // check *channels_in_file to see if it's trivially opaque because e.g. there were only 3 channels in the source image.

    int width            = 0,
        height           = 0,
        channels_in_file = 0,
        desired_channels = STBI_rgb_alpha;

    SKIV_STBI_CICP       = { };
    SKIV_STBI_SBIT       = { };
    SKIV_STBI_ResultInfo = { };
    SKIV_STBI_srgb       = false;

// Stop using STB, it was convenient to parse a few of the chunks,
//   but it cannot be used for anything else due to gamma issues.
//#define HAS_WORKING_STB_GAMMA
#define STBI_FLOAT
#if defined (STBI_FLOAT) || !defined(HAS_WORKING_STB_GAMMA)
    // Check whether the image is a HDR image or not
    image.light_info.isHDR = stbi_is_hdr_from_file (pImageFile);

    PLOG_VERBOSE << "STBI thinks the image is... " << ((image.light_info.isHDR) ? "HDR" : "SDR");

    fseek  (pImageFile,                                 0, SEEK_SET  );
    fread  (_scratchMemory.get (), _.getInitialSize (), 1, pImageFile);
    rewind (pImageFile);

    // HDR10 PNGs (Rec. 2020 primaries, PQ): by their cICP chunk, or the cICP of their ICC profile (as SKIV and Special K
    //   write them), see _IsHDR10ColorSource. Decoded by WIC below, then converted to scRGB. Every other PNG is SDR.
    const bool hdr10 = (image_sig->mime_type == L"image/png") && _IsHDR10ColorSource (image.color_source);

    // HLG PNGs (BT.2100: Rec. 2020, Rec. 709 or Display P3 primaries), by their cICP or their ICC profile's cicp tag: as
    //   HDR10 ones, decoded by WIC, then converted (for a 1,000-nit display, see _HLGToDisplay)
    const uint8_t hlgPrimaries = (image_sig->mime_type == L"image/png" && ! hdr10) ? _HLGPrimaries (image.color_source) : 0;
    const bool    hlg          = (hlgPrimaries != 0);
    const bool    cicp         = hdr10 || hlg;

    SKIV_STBI_ICCP.iCCP = ! image.color_source.icc.empty ();

    // The significant bits of a PNG (sBIT), from its parsed chunks (see _ReadColorSourcePNG): the bytes as the chunk has
    //   them, red to alpha (a gray image's in red)
    if (image_sig->mime_type == L"image/png")
      memcpy (&SKIV_STBI_SBIT, image.color_source.sbit, std::min <size_t> (image.color_source.sbit_count, sizeof (SKIV_STBI_SBIT)));

    float*                pixels = cicp ?
                                 nullptr :
                                   stbi_loadf_from_memory (_scratchMemory.get (), static_cast <int> (_.getInitialSize ()), &width, &height, &channels_in_file, desired_channels);

    // stb_image reads a cICP chunk into SKIV_STBI_CICP itself: only HDR10 and HLG are taken from it (others crashed the
    //   HDR path)
    if (hdr10 || hlg)
    {
      SKIV_STBI_CICP.primaries     = (hlg) ? hlgPrimaries :  9;
      SKIV_STBI_CICP.transfer_func = (hlg) ?           18 : 16;
      SKIV_STBI_CICP.matrix_coeffs =  0;
    }

    else
      SKIV_STBI_CICP = { };
    typedef float         pixel_size;
    DXGI_FORMAT           dxgi_format = DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT;
#ifndef STBI_FLOAT
    unsigned char*        pixels = stbi_load  (szPath.c_str(), &width, &height, &channels_in_file, desired_channels);
    typedef unsigned char pixel_size;
    constexpr DXGI_FORMAT dxgi_format = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;
#endif

    // Fall back to using WIC if STB fails to parse the file
    if (pixels == NULL && SKIV_STBI_CICP.primaries == 0)
    {
      decoder = ImageDecoder_WIC;
      PLOG_ERROR << "Using WIC decoder due to STB failing with: " << stbi_failure_reason();
    }

    else if (pixels != nullptr && SKIV_STBI_srgb && !cicp && !SKIV_STBI_ICCP.iCCP)
    {
      decoder = ImageDecoder_WIC;
      PLOG_ERROR << "Using WIC decoder due to STB incorrectly handling sRGB";
    }
#endif

    else
    {
#ifdef _DEBUG

      PLOG_VERBOSE << "SKIV_STBI_cICP:";
      PLOG_VERBOSE << ".primaries    : " << (int)SKIV_STBI_CICP.primaries;
      PLOG_VERBOSE << ".transfer_func: " << (int)SKIV_STBI_CICP.transfer_func;
      PLOG_VERBOSE << ".matrix_coeffs: " << (int)SKIV_STBI_CICP.matrix_coeffs;
      PLOG_VERBOSE << ".full_range   : " << (int)SKIV_STBI_CICP.full_range;

#endif // _DEBUG

      if (SKIV_STBI_CICP.primaries != 0)
      {
        if (SKIV_STBI_SBIT.red_bits > 8)
          dxgi_format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        else
          dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

        assert (SKIV_STBI_CICP.transfer_func == 16 || SKIV_STBI_CICP.transfer_func == 18); // ST 2084, or HLG
        assert (SKIV_STBI_CICP.matrix_coeffs ==  0); // Identity
        // RGB is currently the only supported color model in PNG,
        //   and as such Matrix Coefficients shall be set to 0.
        //
        // But presumably it may eventually also be:
        //    0 (RGB)
        //    9 (BT.2020 Non-Constant Luminance)
        //   10 (BT.2020 Constant Luminance)
        //   14 (BT.2100 ICtCp)

        image.light_info.isHDR = true;
        image.is_hdr           = true;
      }

      meta.width     = width;
      meta.height    = height;
      meta.depth     = 1;
      meta.arraySize = 1;
      meta.mipLevels = 1;
      meta.format    = dxgi_format; // STBI_rgb_alpha
      meta.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

      if ( dxgi_format == DXGI_FORMAT_R32G32B32A32_FLOAT  ||
         ((dxgi_format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
           dxgi_format == DXGI_FORMAT_R16G16B16A16_FLOAT) && image.is_hdr))
      {
#ifndef HAS_WORKING_STB_GAMMA
        if (! ((dxgi_format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                dxgi_format == DXGI_FORMAT_R16G16B16A16_FLOAT) && image.is_hdr))
        {
          decoder = ImageDecoder_WIC;
          PLOG_ERROR << "Using WIC decoder due to STB incorrectly handling sRGB";
        }
#endif
        // Good grief this is inefficient, let's convert it to something reasonable...
        DirectX::ScratchImage raw_fp32_img;

        // Check for BT.2020 using ST.2084 (HDR10), or HLG
        if ( (SKIV_STBI_CICP.primaries     ==  9 &&
              SKIV_STBI_CICP.transfer_func == 16) || hlg )
        {
          DirectX::ScratchImage temp_img  = { };
          DirectX::ScratchImage temp_img2 = { };

          if (SUCCEEDED (
              DirectX::LoadFromWICMemory (
                _scratchMemory.get (), _.getInitialSize (),
                  DirectX::WIC_FLAGS_FILTER_POINT | DirectX::WIC_FLAGS_FORCE_LINEAR,
                    &meta, temp_img)))
          {
            image.bpc      =
              (int)DirectX::BitsPerColor (meta.format);
            image.channels =
              DirectX::HasAlpha     (meta.format) ? 4 : 3; // Expect 3... 4 would be weird for an HDR image

            PLOG_INFO << ((hlg) ? "HLG PNG detected (decoded as for a 1,000-nit display), transforming to scRGB..."
                                : "HDR10 PNG detected, transforming to scRGB...");

            const float* weights = _LuminanceWeights (hlgPrimaries);

            // PNG will be loaded as UNORM, we need to convert to float...
            if (SUCCEEDED (DirectX::Convert        (*temp_img.GetImages (), DXGI_FORMAT_R16G16B16A16_FLOAT, DirectX::TEX_FILTER_DEFAULT, 0.0f, temp_img2)))
            if (SUCCEEDED (img.InitializeFromImage (*temp_img2.GetImage (0,0,0))))
            {
              using namespace DirectX;

              TransformImage ( temp_img2.GetImages     (),
                               temp_img2.GetImageCount (),
                               temp_img2.GetMetadata   (),
              [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t width, size_t y)
              {
                UNREFERENCED_PARAMETER(y);

                for (size_t j = 0; j < width; ++j)
                {
                  XMVECTOR v = inPixels [j];
                  XMVECTOR r;

                  if (! hlg)
                    r = XMVector3Transform (SKIV_Image_PQToLinear (v), c_Bt2100toscRGB);

                  else
                  {
                    // Display light (1.0: 10,000 nits), then scRGB (1.0: 80 nits) of its primaries
                    const float signal [3] = { XMVectorGetX (v), XMVectorGetY (v), XMVectorGetZ (v) };
                    float       light  [3];
                    _HLGToDisplay (signal, weights, light);

                    const XMVECTOR l = XMVectorSet (light [0], light [1], light [2], 0.0f);

                    r = (hlgPrimaries == 1)  ? XMVectorScale (l, 125.0f) :
                        (hlgPrimaries == 12) ? XMVectorScale (XMVector3Transform (l, c_fromDCIP3to709), 125.0f)
                                             : XMVector3Transform (l, c_Bt2100toscRGB);
                  }

                  outPixels[j] = XMVectorSetW(r, XMVectorGetW(v));
                }
              }, img );

              meta.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
              converted   = true;
              succeeded   = true;

              if (SKIV_STBI_SBIT.red_bits != 0)
              {
                image.bpc = SKIV_STBI_SBIT.red_bits;

                image.channels = 0;

                if (SKIV_STBI_SBIT.red_bits   > 0) image.channels++;
                if (SKIV_STBI_SBIT.green_bits > 0) image.channels++;
                if (SKIV_STBI_SBIT.blue_bits  > 0) image.channels++;
                if (SKIV_STBI_SBIT.alpha_bits > 0) image.channels++;
              }
            }
          }
        }

        if ((! converted) && SUCCEEDED (raw_fp32_img.Initialize2D (meta.format, width, height, 1, 1)))
        {
          size_t   imageSize = width * height * desired_channels * sizeof (pixel_size);
          uint8_t* pDest     = raw_fp32_img.GetImage (0, 0, 0)->pixels;
          memcpy  (pDest, pixels, imageSize);

          image.bpc      = SKIV_STBI_ResultInfo.bits_per_channel;
          image.channels = channels_in_file;

          if (image.bpc > 32)
              image.bpc = 32;

          // Still overkill for SDR, but we're saving some VRAM...
          const DXGI_FORMAT final_format =
            image.bpc <=  8 ? //image.channels == 1 ? DXGI_FORMAT_R16_UNORM          :
                              //image.channels == 2 ? DXGI_FORMAT_R16G16_UNORM       :
                              //image.channels == 3 ? DXGI_FORMAT_R16G16B16A16_UNORM :
                              //image.channels == 4 ? DXGI_FORMAT_R16G16B16A16_UNORM
                              //                      : DXGI_FORMAT_UNKNOWN
                              image.channels == 1 ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB            :
                              image.channels == 2 ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB          :
                              image.channels == 3 ? DXGI_FORMAT_B8G8R8X8_UNORM_SRGB :
                              image.channels == 4 ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                                  : DXGI_FORMAT_UNKNOWN
                            :
            image.bpc <= 10 ? //image.channels == 1 ? DXGI_FORMAT_R16_UNORM          :
                              //image.channels == 2 ? DXGI_FORMAT_R16G16_UNORM       :
                              //image.channels == 3 ? DXGI_FORMAT_R16G16B16A16_UNORM :
                              //image.channels == 4 ? DXGI_FORMAT_R16G16B16A16_UNORM
                              //                      : DXGI_FORMAT_UNKNOWN
                              image.channels == 1 ? DXGI_FORMAT_R10G10B10A2_UNORM :
                              image.channels == 2 ? DXGI_FORMAT_R10G10B10A2_UNORM :
                              image.channels == 3 ? DXGI_FORMAT_R10G10B10A2_UNORM :
                              image.channels == 4 ? DXGI_FORMAT_R10G10B10A2_UNORM
                                                  : DXGI_FORMAT_UNKNOWN
                            :
            image.bpc == 16 ? image.channels == 1 ? DXGI_FORMAT_R16G16B16A16_UNORM          :
                              image.channels == 2 ? DXGI_FORMAT_R16G16B16A16_UNORM       :
                              image.channels == 3 ? DXGI_FORMAT_R16G16B16A16_UNORM :
                              image.channels == 4 ? DXGI_FORMAT_R16G16B16A16_UNORM
                                                    : DXGI_FORMAT_UNKNOWN
                            :
            image.bpc >= 32 ? image.channels == 1 ? DXGI_FORMAT_R32G32B32A32_FLOAT       :
                              image.channels == 2 ? DXGI_FORMAT_R32G32B32A32_FLOAT    :
                              image.channels == 3 ? DXGI_FORMAT_R32G32B32A32_FLOAT :
                              image.channels == 4 ? DXGI_FORMAT_R32G32B32A32_FLOAT
                                                    : DXGI_FORMAT_UNKNOWN
                                                    : DXGI_FORMAT_UNKNOWN;

          if (SUCCEEDED (DirectX::Convert (*raw_fp32_img.GetImages (), final_format, DirectX::TEX_FILTER_DEFAULT, 0.0f, img)))
          {
            meta.format = final_format;
            converted   = true;
            succeeded   = true;
          }
        }
      }

      if (converted == false && SUCCEEDED (img.Initialize2D (meta.format, width, height, 1, 1)))
      {
        image.bpc      = SKIV_STBI_ResultInfo.bits_per_channel;
        image.channels = channels_in_file;

        size_t   imageSize = width * height * desired_channels * sizeof (pixel_size);
        uint8_t* pDest     = img.GetImage(0, 0, 0)->pixels;
        memcpy  (pDest, pixels, imageSize);

        succeeded = true;
      }

      stbi_image_free (pixels);

      //decoder = ImageDecoder_WIC;
      //PLOG_INFO << "Using WIC decoder";
    }
  }

  // WebP still images (and an animated one's first frame): libwebp, or WIC if it cannot decode it
  if (decoder == ImageDecoder_WebP)
  {
    fseek  (pImageFile,                                 0, SEEK_SET  );
    fread  (_scratchMemory.get (), _.getInitialSize (), 1, pImageFile);
    rewind (pImageFile);

    uint32_t              width    = 0,
                          height   = 0;
    std::vector <uint8_t> rgba;
    WebPBitstreamFeatures features = { };

    if (_DecodeWebPStill (_scratchMemory.get (), _.getInitialSize (), width, height, rgba) &&
        WebPGetFeatures  (_scratchMemory.get (), _.getInitialSize (), &features) == VP8_STATUS_OK &&
        SUCCEEDED (img.Initialize2D (DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, width, height, 1, 1)))
    {
      memcpy (img.GetPixels (), rgba.data (), std::min (rgba.size (), img.GetPixelsSize ()));

      meta           = img.GetMetadata ();
      image.bpc      = 8;
      image.channels = (features.has_alpha) ? 4 : 3;

      succeeded      = true;
    }

    else
    {
      decoder = ImageDecoder_WIC;
      PLOG_ERROR << "Using WIC decoder due to libwebp failing to decode the image";
    }
  }

  if (decoder == ImageDecoder_WIC)
  {
    fseek  (pImageFile,                                 0, SEEK_SET  );
    fread  (_scratchMemory.get (), _.getInitialSize (), 1, pImageFile);
    rewind (pImageFile);

    if (SUCCEEDED (
        DirectX::LoadFromWICMemory (
          _scratchMemory.get (), _.getInitialSize (),
            DirectX::WIC_FLAGS_FILTER_POINT | DirectX::WIC_FLAGS_IGNORE_SRGB, // sRGB or not: decided below
              &meta, img,
          [&](IWICMetadataQueryReader* metadataReader) noexcept
          {
            if (!metadataReader) return;

            PROPVARIANT value;
            PropVariantInit(&value);

            if (SUCCEEDED(metadataReader->GetMetadataByName(L"/sRGB/RenderingIntent", &value)))
            {
              // Presence of this data indicates that the image cannot be HDR
              image.not_hdr = true;
              PropVariantClear(&value);
            }

          })))
    {
      image.bpc      =
        (int)DirectX::BitsPerColor (meta.format);
      image.channels =
        DirectX::HasAlpha          (meta.format) ? 4 : 3; // 2 and 1 channel images are unsupported for now

      // HDR10 (TIFF, JPEG XR...): only when the file says so, not every 16-bit image without a PNG sRGB chunk
      if (image.bpc >= 16 &&
          meta.format == DXGI_FORMAT_R16G16B16A16_UNORM &&
          _IsHDR10ColorSource (image.color_source))
      {
        image.is_hdr = true;
        image.light_info.isHDR = true;

        DirectX::ScratchImage temp_img2 = { };

        PLOG_INFO << "HDR image detected";
        // PNG will be loaded as UNORM, we need to convert to float...
        if (SUCCEEDED(DirectX::Convert(*img.GetImages(), DXGI_FORMAT_R16G16B16A16_FLOAT, DirectX::TEX_FILTER_DEFAULT, 0.0f, temp_img2)))
          if (SUCCEEDED(img.InitializeFromImage(*temp_img2.GetImage(0, 0, 0))))
          {
            using namespace DirectX;
            TransformImage(temp_img2.GetImages(),
              temp_img2.GetImageCount(),
              temp_img2.GetMetadata(),
              [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t width, size_t y)
              {
                UNREFERENCED_PARAMETER(y);

                for (size_t j = 0; j < width; ++j)
                {
                  XMVECTOR v = inPixels[j];

                  XMVECTOR r =
                    XMVector3Transform(SKIV_Image_PQToLinear(v), c_Bt2100toscRGB);

                  outPixels[j] = XMVectorSetW(r, XMVectorGetW(v));
                }

              }, img);

            meta.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            converted = true;
            succeeded = true;
          }
      }

      if (image.is_hdr && (image_sig->mime_type == L"image/vnd.ms-photo" ||
                           image_sig->mime_type == L"image/avif"))
      {
        if (DirectX::BitsPerColor (meta.format) < 16)
        {
          PLOG_INFO << "HDR capable image format does not contain HDR pixels... demoting to SDR!";

          image.is_hdr = false; // Made sRGB below
        }

        else
        {
          image.light_info.isHDR = true;
        }
      }

      // SDR images (integer formats): their colors are sRGB, unless the file says they are linear. The shaders take the
      //   texture's values as linear, so they become an _SRGB format (8 bpc), or are made linear here (more bits).
      //     DirectXTex is not asked (WIC_FLAGS_IGNORE_SRGB): it takes any image without Exif ColorSpace 1 or a PNG sRGB
      //       chunk for a linear one, so pictures with ColorSpace 65535 (uncalibrated) showed washed out.
      //         Images of other colors (wide gamut: see color_convert.inl) are left as they are decoded (as RGBA), and
      //           converted to scRGB after all the decoders.
      const bool sdr_wide =
        (! image.is_hdr && DirectX::FormatDataType (meta.format) == DirectX::FORMAT_TYPE_UNORM &&
           _NeedsColorConversion (image.color_source));

      const bool sdr_srgb =
        (! image.is_hdr && ! sdr_wide && ! DirectX::IsSRGB (meta.format) &&
           DirectX::FormatDataType (meta.format) == DirectX::FORMAT_TYPE_UNORM &&
         ! _IsLinearColorSource (image.color_source));

      // Grayscale, and formats of fewer bits (or without an _SRGB form): as RGBA of 8 or 16 bpc
      if ((sdr_srgb || sdr_wide) && DirectX::MakeSRGB (meta.format) == meta.format && meta.format != DXGI_FORMAT_R16G16B16A16_UNORM)
      {
        const DXGI_FORMAT rgba = (DirectX::BitsPerColor (meta.format) <= 8) ? DXGI_FORMAT_R8G8B8A8_UNORM
                                                                            : DXGI_FORMAT_R16G16B16A16_UNORM;
        DirectX::ScratchImage expanded;

        if (SUCCEEDED (DirectX::Convert (*img.GetImages (), rgba, DirectX::TEX_FILTER_DEFAULT, 0.0f, expanded)))
        {
          std::swap (img, expanded);
          meta.format = rgba;
        }
      }

      if (sdr_srgb && DirectX::MakeSRGB (meta.format) != meta.format)
      {
        if (img.OverrideFormat (DirectX::MakeSRGB (meta.format)))
          meta.format = DirectX::MakeSRGB (meta.format);
      }

      else if (sdr_srgb)
      {
        using namespace DirectX;

        TransformImage ( *img.GetImages (),
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
                XMColorSRGBToRGB (
                  XMVectorSaturate (value)
                );
            }
          }, img_srgb
        );

        std::swap (img, img_srgb);
      }

      succeeded = true;
    }
  }

  if (decoder == ImageDecoder_DDS)
  {
    fseek  (pImageFile,                                 0, SEEK_SET  );
    fread  (_scratchMemory.get (), _.getInitialSize (), 1, pImageFile);
    rewind (pImageFile);

    if (SUCCEEDED (
        DirectX::LoadFromDDSMemory (
          _scratchMemory.get (), _.getInitialSize (),
            DirectX::DDS_FLAGS_PERMISSIVE,
              &meta, img)))
    {
      succeeded = true;
      meta = img.GetMetadata ( );

      image.bpc      = static_cast <int> (DirectX::BitsPerColor      (meta.format));
      image.channels =                    SKIV_DXGI_NumberOfChannels (meta.format);
      image.is_dds   = true;

      if (DirectX::MakeTypeless (meta.format) == DXGI_FORMAT_BC6H_TYPELESS)
      {
        image.is_hdr = true;
      }
    }
  }

#ifdef OpenEXR
#ifdef _M_X64
  if (decoder == ImageDecoder_EXR)
  {
    using namespace DirectX;

    TexMetadata exr_meta;

    if (SUCCEEDED (LoadFromEXRFile (imagePath.c_str (), &exr_meta, img)))
    {
      image.bpc      = static_cast <int> (DirectX::BitsPerColor (exr_meta.format));
      image.channels =               3 + (DirectX::HasAlpha     (exr_meta.format) ?
                                                                                1 : 0);

      succeeded = true;

      image.light_info.isHDR = true;
      image.is_hdr           = true;

      image.width    = static_cast <float> (exr_meta.width);
      image.height   = static_cast <float> (exr_meta.height);

      meta.format    = DXGI_FORMAT_R16G16B16A16_FLOAT;
      meta.width     = static_cast <size_t> (image.width);
      meta.height    = static_cast <size_t> (image.height);
      meta.depth     = 1;
      meta.arraySize = 1;
      meta.mipLevels = 1;
      meta.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
    }
  }
#endif
#endif

  if (decoder == ImageDecoder_HDR)
  {
    fseek  (pImageFile,                                 0, SEEK_SET  );
    fread  (_scratchMemory.get (), _.getInitialSize (), 1, pImageFile);
    rewind (pImageFile);

    using namespace DirectX;

    TexMetadata hdr_meta;

    if (SUCCEEDED (LoadFromHDRMemory (_scratchMemory.get (), _.getInitialSize (), &hdr_meta, img)))
    {
      image.bpc      = static_cast <int> (DirectX::BitsPerColor (hdr_meta.format));
      image.channels =               3 + (DirectX::HasAlpha     (hdr_meta.format) ?
                                                                                1 : 0);

      succeeded = true;

      image.light_info.isHDR = true;
      image.is_hdr           = true;

      image.width    = static_cast <float> (hdr_meta.width);
      image.height   = static_cast <float> (hdr_meta.height);

      meta.format    = hdr_meta.format;
      meta.width     = static_cast <size_t> (image.width);
      meta.height    = static_cast <size_t> (image.height);
      meta.depth     = 1;
      meta.arraySize = 1;
      meta.mipLevels = 1;
      meta.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
    }
  }

  if (decoder == ImageDecoder_AVIF)
  {
    if (! isAVIFEncoderAvailable ())
    {
      PLOG_ERROR << L"Unworkable libavif DLL present, will attempt to re-download the next time an AVIF image is decoded.";
    }

    else
    {
      auto avif_decoder =
        SK_avifDecoderCreate ();

      SYSTEM_INFO     si = { };
      GetSystemInfo (&si);

#if _WIN64
      avif_decoder->maxThreads =
        std::min (64U, std::min ((UINT)si.dwNumberOfProcessors, (UINT)__popcnt64 (si.dwActiveProcessorMask)));
#else
      avif_decoder->maxThreads =
        std::min (32U, std::min ((UINT)si.dwNumberOfProcessors, (UINT)__popcnt   (si.dwActiveProcessorMask)));
#endif

      fseek  (pImageFile,                                 0, SEEK_SET  );
      fread  (_scratchMemory.get (), _.getInitialSize (), 1, pImageFile);
      rewind (pImageFile);

      SK_avifDecoderSetIOMemory (avif_decoder, _scratchMemory.get (), _.getInitialSize ());

      if (SK_avifDecoderParse (avif_decoder) != AVIF_RESULT_OK)
        PLOG_ERROR << "libavif could not read the image: " << avif_decoder->diag.error;

      // We only want 1 image, if there are more... too bad.
      if (SK_avifDecoderNextImage  (avif_decoder) == AVIF_RESULT_OK)
      {
        avifRGBImage                 rgb;
        SK_avifRGBImageSetDefaults (&rgb, avif_decoder->image);

        // Its color information: the ICC profile and nclx (cICP), both of which it may have
        image.color_source.origin     = "AVIF";
        image.color_source.has_cicp   = (avif_decoder->image->colorPrimaries          != AVIF_COLOR_PRIMARIES_UNSPECIFIED ||
                                         avif_decoder->image->transferCharacteristics != AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED);
        image.color_source.primaries  = static_cast <uint8_t> (avif_decoder->image->colorPrimaries);
        image.color_source.transfer   = static_cast <uint8_t> (avif_decoder->image->transferCharacteristics);
        image.color_source.matrix     = static_cast <uint8_t> (avif_decoder->image->matrixCoefficients);
        image.color_source.full_range = (avif_decoder->image->yuvRange == AVIF_RANGE_FULL) ? 1 : 0;

        _SetICC (image.color_source, avif_decoder->image->icc.data, avif_decoder->image->icc.size);

        int bpc = rgb.depth;
        // Of more than 8 bpc, but of an SDR curve (Rec. 709, sRGB, a gamma, linear...): SDR, converted to scRGB below from
        //   its colors (cICP or ICC profile). The HDR conversions take every image of more than 8 bpc for PQ.
        const uint32_t transfer = avif_decoder->image->transferCharacteristics;
        const bool     sdr_high_bit =
          (avif_decoder->image->depth > 8) &&
          (transfer ==  1 || transfer ==  4 || transfer ==  5 || transfer ==  6 || transfer ==  7 ||
           transfer ==  8 || transfer == 13 || transfer == 14 || transfer == 15 || transfer == 17);

        bool is_hdr_image = (avif_decoder->image->depth > 8 && ! sdr_high_bit) ||
          (avif_decoder->image->transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084);

        if (avif_decoder->image->colorPrimaries == AVIF_COLOR_PRIMARIES_UNSPECIFIED)
        {
          //ideally try to retrieve primaries from icc here
        }

        bool bHasAlpha = avif_decoder->image->alphaPlane ? true : false;

        DXGI_FORMAT dxgi_format = is_hdr_image  ? DXGI_FORMAT_R16G16B16A16_FLOAT  :
                                  sdr_high_bit  ? DXGI_FORMAT_R16G16B16A16_UNORM  :
                                     bHasAlpha  ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB :
                                                  DXGI_FORMAT_B8G8R8X8_UNORM_SRGB ;

        rgb.depth       = (is_hdr_image || sdr_high_bit) ? 16 : 8;
        rgb.format      = (is_hdr_image || sdr_high_bit) ? AVIF_RGB_FORMAT_RGBA : AVIF_RGB_FORMAT_BGRA;
#if _WIN64
        rgb.maxThreads  = std::min (64U, std::min ((UINT)si.dwNumberOfProcessors, (UINT)__popcnt64 (si.dwActiveProcessorMask)));
#else
        rgb.maxThreads  = std::min (32U, std::min ((UINT)si.dwNumberOfProcessors, (UINT)__popcnt   (si.dwActiveProcessorMask)));
#endif
        rgb.ignoreAlpha = false;
        rgb.isFloat     = is_hdr_image ? true : false;

        SK_avifRGBImageAllocatePixels (                     &rgb);
        SK_avifImageYUVToRGB          (avif_decoder->image, &rgb);

        image.width    = static_cast <float> (rgb.width);
        image.height   = static_cast <float> (rgb.height);

        DirectX::ScratchImage temp_img;

        if (SUCCEEDED (temp_img.Initialize2D (dxgi_format, static_cast <size_t> (image.width),
                                                           static_cast <size_t> (image.height), 1, 1)))
        {
          using namespace DirectX;

          image.channels = bHasAlpha ? 4 : 3;
          image.bpc      = bpc;

          // XXX
          image.light_info.isHDR = is_hdr_image;
          image.is_hdr           = is_hdr_image;

          succeeded      = true;

          meta.format    = dxgi_format;
          meta.width     = static_cast <size_t> (image.width);
          meta.height    = static_cast <size_t> (image.height);
          meta.depth     = 1;
          meta.arraySize = 1;
          meta.mipLevels = 1;
          meta.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

          void*  pixels_buffer      = static_cast <void *>(temp_img.GetPixels ());

#ifdef _DEBUG
          size_t pixels_buffer_size = temp_img.GetPixelsSize ();
          assert (pixels_buffer_size >= rgb.rowBytes * rgb.height);
#endif

          memcpy (pixels_buffer, rgb.pixels, rgb.rowBytes * rgb.height);

          if (avif_decoder->image->transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED)
          {
            notices.push_back (ImGuiToast
              {
                ImGuiToastType::Error,
                10000,
                "Unspecified AVIF Transfer Characteristics: %d",
                avif_decoder->image->transferCharacteristics
              }
            );
          }

          // SDR of more than 8 bpc: its encoded values (16-bit) converted to scRGB, from its colors (sRGB included); a
          //   linear Rec. 709 one is drawn as it is
          if (sdr_high_bit)
          {
            cmsHPROFILE           source = _CreateSDRSourceProfile (image.color_source);
            const DirectX::Image* in     = temp_img.GetImage (0, 0, 0);
            bool                  done   = (source == nullptr);

            if (source != nullptr)
            {
              if (SUCCEEDED (img.Initialize2D (DXGI_FORMAT_R16G16B16A16_FLOAT, in->width, in->height, 1, 1)) &&
                  _ConvertToScRGB (source, TYPE_RGBA_16, in->pixels, in->rowPitch, img.GetPixels (), img.GetImage (0, 0, 0)->rowPitch,
                                   static_cast <uint32_t> (in->width), static_cast <uint32_t> (in->height)))
              {
                meta.format  = DXGI_FORMAT_R16G16B16A16_FLOAT;
                image.is_wcg = _NeedsColorConversion (image.color_source);
                done         = true;

                PLOG_INFO << "[Image Processing] Converted the " << avif_decoder->image->depth << "-bpc AVIF image's colors ("
                          << _DescribeColorSource (image.color_source) << ") to scRGB";
              }

              cmsCloseProfile (source);
            }

            PLOG_WARNING_IF (! done) << "Could not convert the " << avif_decoder->image->depth << "-bpc AVIF image's colors";

            if (meta.format != DXGI_FORMAT_R16G16B16A16_FLOAT)
              std::swap (img, temp_img);

            temp_img.Release ();
          }

          // HDR: the signal to display light (1.0: 10,000 nits), by its transfer: HLG (BT.2100, for a 1,000-nit display,
          //   see _HLGToDisplay, on the luminance of its own primaries), or else PQ. The primaries are converted below.
          const bool hlg =
            (avif_decoder->image->transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_HLG);

          const float* lumaWeights = [&](void) -> const float*
          {
            static constexpr float bt2020 [3] = { 0.2627f, 0.6780f, 0.0593f },
                                   bt709  [3] = { 0.2126f, 0.7152f, 0.0722f },
                                   p3     [3] = { 0.2290f, 0.6917f, 0.0793f },
                                   bt601  [3] = { 0.2124f, 0.7011f, 0.0866f },
                                   xyz    [3] = { 0.0f,    1.0f,    0.0f    };

            switch (avif_decoder->image->colorPrimaries)
            {
              case AVIF_COLOR_PRIMARIES_XYZ:    return xyz;
              case AVIF_COLOR_PRIMARIES_BT601:  return bt601;
              case AVIF_COLOR_PRIMARIES_DCI_P3: return p3;
              case AVIF_COLOR_PRIMARIES_BT709:  return bt709;
              default:                          return bt2020;
            }
          } ( );

          auto _ToLinear = [&](XMVECTOR v) -> XMVECTOR
          {
            if (! hlg)
              return SKIV_Image_PQToLinear (v);

            const float signal [3] = { XMVectorGetX (v), XMVectorGetY (v), XMVectorGetZ (v) };
            float       light  [3];

            _HLGToDisplay (signal, lumaWeights, light);
            return XMVectorSet (light [0], light [1], light [2], XMVectorGetW (v));
          };

          PLOG_INFO_IF (hlg) << "[Image Processing] HLG AVIF image: decoded as for a 1,000-nit display";

          if (avif_decoder->image->colorPrimaries == AVIF_COLOR_PRIMARIES_XYZ)
          {
            if ( SUCCEEDED ( TransformImage (*temp_img.GetImages (),
                  [&](      XMVECTOR* outPixels,
                      const XMVECTOR* inPixels,
                            size_t    width,
                            size_t    y)
                  {
                    UNREFERENCED_PARAMETER(y);

                    for (size_t j = 0; j < width; ++j)
                    {
                      XMVECTOR v = inPixels [j];

                      XMVECTOR r =
                        XMVectorScale (
                          XMVector3Transform (_ToLinear (v), c_fromXYZto709), 125.0f
                        );

                      outPixels [j] = XMVectorSetW (r, XMVectorGetW (v));
                    }
                  }, img )
                )
              )
            {
              temp_img.Release ();
            }
          }

          else if (avif_decoder->image->colorPrimaries == AVIF_COLOR_PRIMARIES_BT601)
          {
            if ( SUCCEEDED ( TransformImage (*temp_img.GetImages (),
                  [&](      XMVECTOR* outPixels,
                      const XMVECTOR* inPixels,
                            size_t    width,
                            size_t    y)
                  {
                    UNREFERENCED_PARAMETER(y);

                    for (size_t j = 0; j < width; ++j)
                    {
                      XMVECTOR v = inPixels [j];

                      XMVECTOR r =
                        XMVectorScale (
                          XMVector3Transform (_ToLinear (v), c_from601to709), 125.0f
                        );

                      outPixels [j] = XMVectorSetW (r, XMVectorGetW (v));
                    }
                  }, img )
                )
              )
            {
              temp_img.Release ();
            }
          }

          else if (avif_decoder->image->colorPrimaries == AVIF_COLOR_PRIMARIES_DCI_P3)
          {
            if ( SUCCEEDED ( TransformImage (*temp_img.GetImages (),
                  [&](      XMVECTOR* outPixels,
                      const XMVECTOR* inPixels,
                            size_t    width,
                            size_t    y)
                  {
                    UNREFERENCED_PARAMETER(y);

                    for (size_t j = 0; j < width; ++j)
                    {
                      XMVECTOR v = inPixels [j];

                      XMVECTOR r =
                        XMVectorScale (
                          XMVector3Transform (_ToLinear (v), c_fromDCIP3to709), 125.0f
                        );

                      outPixels [j] = XMVectorSetW (r, XMVectorGetW (v));
                    }
                  }, img )
                )
              )
            {
              temp_img.Release ();
            }
          }

          else if (avif_decoder->image->colorPrimaries == AVIF_COLOR_PRIMARIES_BT709 ||
                   avif_decoder->image->colorPrimaries == AVIF_COLOR_PRIMARIES_SRGB)
          {
            if (avif_decoder->image->transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_PQ ||
                avif_decoder->image->transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084 || hlg)
            {
              if ( SUCCEEDED ( TransformImage(*temp_img.GetImages (),
                    [&](      XMVECTOR* outPixels,
                        const XMVECTOR* inPixels,
                              size_t    width,
                              size_t    y)
                {
                  UNREFERENCED_PARAMETER(y);

                  for (size_t j = 0; j < width; ++j)
                  {
                    XMVECTOR v = inPixels[j];

                    XMVECTOR r =
                      XMVectorScale(
                        _ToLinear (v), 125.0f
                      );

                    outPixels[j] = XMVectorSetW (r, XMVectorGetW (v));
                  }
                }, img)
              )
                )
              {
                temp_img.Release ();
              }
            }
            else
            {
              std::swap(img, temp_img);
              temp_img.Release ();
            }
          }

          else
          {
            if (avif_decoder->image->colorPrimaries == AVIF_COLOR_PRIMARIES_UNSPECIFIED) {
              notices.push_back (ImGuiToast
                {
                  ImGuiToastType::Error,
                  10000,
                  "Unspecified AVIF Color Primaries: %d",
                  avif_decoder->image->transferCharacteristics
                }
              );
            }
            else if (avif_decoder->image->colorPrimaries != AVIF_COLOR_PRIMARIES_BT2100 &&
                avif_decoder->image->colorPrimaries != AVIF_COLOR_PRIMARIES_BT2020)
            {
              notices.push_back (ImGuiToast
                {
                  ImGuiToastType::Error,
                  15000,
                  "Unsupported AVIF Color Primaries: %d",
                  avif_decoder->image->colorPrimaries
                }
              );
            }
            //HDR 8-bpc is not handled correctly
            if (bpc == 8 && avif_decoder->image->colorPrimaries == AVIF_COLOR_PRIMARIES_UNSPECIFIED) {
              //for now treat it as SDR, but that's incorrect measure in long run
              std::swap(img, temp_img);
              temp_img.Release();
            }
            //everything that reached here is treated as HDR image
            else if ( SUCCEEDED ( TransformImage (*temp_img.GetImages (),
                  [&](      XMVECTOR* outPixels,
                      const XMVECTOR* inPixels,
                            size_t    width,
                            size_t    y)
                  {
                    UNREFERENCED_PARAMETER(y);

                    for (size_t j = 0; j < width; ++j)
                    {
                      XMVECTOR v = inPixels [j];

                      XMVECTOR r =
                        XMVector3Transform (_ToLinear (v), c_Bt2100toscRGB);

                      outPixels [j] = XMVectorSetW (r, XMVectorGetW (v));
                    }
                  }, img )
                )
              )
            {
              temp_img.Release ();
            }
          }
        }

        SK_avifRGBImageFreePixels     (                     &rgb);
      }

      SK_avifDecoderDestroy (avif_decoder);
    }
  }

#if _WIN64
  if (decoder == ImageDecoder_JXL)
  {
    static HMODULE hModJXL;
    SK_RunOnce (   hModJXL = LoadLibraryW (L"jxl.dll"));

    static HMODULE hModJXLThreads;
    SK_RunOnce (   hModJXLThreads = LoadLibraryW (L"jxl_threads.dll"));

    using JxlDecoderCreate_pfn                   = JxlDecoder*      (*)(const JxlMemoryManager* memory_manager);
    using JxlDecoderDestroy_pfn                  = void             (*)(      JxlDecoder* dec);
    using JxlDecoderSubscribeEvents_pfn          = JxlDecoderStatus (*)(      JxlDecoder* dec, int events_wanted);
    using JxlDecoderSetInput_pfn                 = JxlDecoderStatus (*)(      JxlDecoder* dec, const uint8_t* data,                        size_t  size);
    using JxlDecoderImageOutBufferSize_pfn       = JxlDecoderStatus (*)(const JxlDecoder* dec, const JxlPixelFormat* format,               size_t* size);
    using JxlDecoderSetImageOutBuffer_pfn        = JxlDecoderStatus (*)(      JxlDecoder* dec, const JxlPixelFormat* format, void* buffer, size_t  size);
    using JxlDecoderSetImageOutBitDepth_pfn      = JxlDecoderStatus (*)(      JxlDecoder* dec, const JxlBitDepth* bit_depth);
    using JxlDecoderGetBasicInfo_pfn             = JxlDecoderStatus (*)(const JxlDecoder* dec, JxlBasicInfo* info);
    using JxlDecoderProcessInput_pfn             = JxlDecoderStatus (*)(      JxlDecoder* dec);
    using JxlDecoderCloseInput_pfn               = void             (*)(      JxlDecoder* dec);
    using JxlDecoderSetPreferredColorProfile_pfn = JxlDecoderStatus (*)(      JxlDecoder* dec, const JxlColorEncoding* color_encoding);
    using JxlDecoderGetColorAsEncodedProfile_pfn = JxlDecoderStatus (*)(const JxlDecoder* dec, JxlColorProfileTarget target, JxlColorEncoding* color_encoding);
    using JxlDecoderGetICCProfileSize_pfn        = JxlDecoderStatus (*)(const JxlDecoder* dec, JxlColorProfileTarget target, size_t* size);
    using JxlDecoderGetColorAsICCProfile_pfn     = JxlDecoderStatus (*)(const JxlDecoder* dec, JxlColorProfileTarget target, uint8_t* icc_profile, size_t size);
    using JxlDecoderSetParallelRunner_pfn        = JxlDecoderStatus (*)(      JxlDecoder* dec,
                                                                        JxlParallelRunner parallel_runner,
                                                                                    void* parallel_runner_opaque);

    using JxlResizableParallelRunnerCreate_pfn         = void*    (*)(const JxlMemoryManager* memory_manager);
    using JxlResizableParallelRunnerSuggestThreads_pfn = uint32_t (*)(uint64_t xsize, uint64_t ysize);
    using JxlResizableParallelRunnerSetThreads_pfn     = void     (*)(void*                  runner_opaque, size_t num_threads);
    using JxlResizableParallelRunnerDestroy_pfn        = void     (*)(void*                  runner_opaque);
    using JxlResizableParallelRunner_pfn   =   JxlParallelRetCode (*)(void*                  runner_opaque,
                                                                      void*                  jpegxl_opaque,
                                                                      JxlParallelRunInit     init,
                                                                      JxlParallelRunFunction func,
                                                                      uint32_t               start_range,
                                                                      uint32_t               end_range);

    JxlDecoderSubscribeEvents_pfn          jxlDecoderSubscribeEvents          = (JxlDecoderSubscribeEvents_pfn)         GetProcAddress (hModJXL, "JxlDecoderSubscribeEvents");
    JxlDecoderSetInput_pfn                 jxlDecoderSetInput                 = (JxlDecoderSetInput_pfn)                GetProcAddress (hModJXL, "JxlDecoderSetInput");
    JxlDecoderImageOutBufferSize_pfn       jxlDecoderImageOutBufferSize       = (JxlDecoderImageOutBufferSize_pfn)      GetProcAddress (hModJXL, "JxlDecoderImageOutBufferSize");
    JxlDecoderCreate_pfn                   jxlDecoderCreate                   = (JxlDecoderCreate_pfn)                  GetProcAddress (hModJXL, "JxlDecoderCreate");
    JxlDecoderDestroy_pfn                  jxlDecoderDestroy                  = (JxlDecoderDestroy_pfn)                 GetProcAddress (hModJXL, "JxlDecoderDestroy");
    JxlDecoderGetBasicInfo_pfn             jxlDecoderGetBasicInfo             = (JxlDecoderGetBasicInfo_pfn)            GetProcAddress (hModJXL, "JxlDecoderGetBasicInfo");
    JxlDecoderProcessInput_pfn             jxlDecoderProcessInput             = (JxlDecoderProcessInput_pfn)            GetProcAddress (hModJXL, "JxlDecoderProcessInput");
    JxlDecoderCloseInput_pfn               jxlDecoderCloseInput               = (JxlDecoderCloseInput_pfn)              GetProcAddress (hModJXL, "JxlDecoderCloseInput");
    JxlDecoderSetImageOutBuffer_pfn        jxlDecoderSetImageOutBuffer        = (JxlDecoderSetImageOutBuffer_pfn)       GetProcAddress (hModJXL, "JxlDecoderSetImageOutBuffer");
    JxlDecoderSetImageOutBitDepth_pfn      jxlDecoderSetImageOutBitDepth      = (JxlDecoderSetImageOutBitDepth_pfn)     GetProcAddress (hModJXL, "JxlDecoderSetImageOutBitDepth");
    JxlDecoderSetParallelRunner_pfn        jxlDecoderSetParallelRunner        = (JxlDecoderSetParallelRunner_pfn)       GetProcAddress (hModJXL, "JxlDecoderSetParallelRunner");
    JxlDecoderSetPreferredColorProfile_pfn jxlDecoderSetPreferredColorProfile = (JxlDecoderSetPreferredColorProfile_pfn)GetProcAddress (hModJXL, "JxlDecoderSetPreferredColorProfile");
    JxlDecoderGetColorAsEncodedProfile_pfn jxlDecoderGetColorAsEncodedProfile = (JxlDecoderGetColorAsEncodedProfile_pfn)GetProcAddress (hModJXL, "JxlDecoderGetColorAsEncodedProfile");
    JxlDecoderGetICCProfileSize_pfn        jxlDecoderGetICCProfileSize        = (JxlDecoderGetICCProfileSize_pfn)       GetProcAddress (hModJXL, "JxlDecoderGetICCProfileSize");
    JxlDecoderGetColorAsICCProfile_pfn     jxlDecoderGetColorAsICCProfile     = (JxlDecoderGetColorAsICCProfile_pfn)    GetProcAddress (hModJXL, "JxlDecoderGetColorAsICCProfile");

    JxlResizableParallelRunnerCreate_pfn         jxlResizableParallelRunnerCreate         = (JxlResizableParallelRunnerCreate_pfn)        GetProcAddress (hModJXLThreads, "JxlResizableParallelRunnerCreate");
    JxlResizableParallelRunnerSuggestThreads_pfn jxlResizableParallelRunnerSuggestThreads = (JxlResizableParallelRunnerSuggestThreads_pfn)GetProcAddress (hModJXLThreads, "JxlResizableParallelRunnerSuggestThreads");
    JxlResizableParallelRunnerSetThreads_pfn     jxlResizableParallelRunnerSetThreads     = (JxlResizableParallelRunnerSetThreads_pfn)    GetProcAddress (hModJXLThreads, "JxlResizableParallelRunnerSetThreads");
    JxlResizableParallelRunnerDestroy_pfn        jxlResizableParallelRunnerDestroy        = (JxlResizableParallelRunnerDestroy_pfn)       GetProcAddress (hModJXLThreads, "JxlResizableParallelRunnerDestroy");
    JxlResizableParallelRunner_pfn               jxlResizableParallelRunner               = (JxlResizableParallelRunner_pfn)              GetProcAddress (hModJXLThreads, "JxlResizableParallelRunner");

    JxlDecoder* jxl_decoder = jxlDecoderCreate                 != nullptr &&
                              jxlResizableParallelRunnerCreate != nullptr  ?
                              jxlDecoderCreate                   (nullptr) : nullptr;
    void*       jxl_runner  = jxlResizableParallelRunnerCreate != nullptr  ?
                              jxlResizableParallelRunnerCreate   (nullptr) : nullptr;

    if ( jxl_decoder != nullptr &&
         jxl_runner  != nullptr &&
         JXL_DEC_SUCCESS ==
           jxlDecoderSubscribeEvents (jxl_decoder, JXL_DEC_BASIC_INFO     |
                                                   JXL_DEC_COLOR_ENCODING |
                                                   JXL_DEC_FULL_IMAGE) &&
         JXL_DEC_SUCCESS ==
           jxlDecoderSetParallelRunner (jxl_decoder, jxlResizableParallelRunner,
                                        jxl_runner) )
    {
      JxlColorEncoding actual_encoding = { };

      JxlBasicInfo   info   = { };
      JxlPixelFormat format =
        { 4, JXL_TYPE_FLOAT, JXL_NATIVE_ENDIAN, 0 };

      auto bpp =
        (format.data_type == JXL_TYPE_FLOAT)   ? (sizeof (float)    * format.num_channels) :
        (format.data_type == JXL_TYPE_UINT8)   ? (sizeof (uint8_t)  * format.num_channels) :
        (format.data_type == JXL_TYPE_UINT16)  ? (sizeof (uint16_t) * format.num_channels) :
        (format.data_type == JXL_TYPE_FLOAT16) ? (sizeof (float)/2) * format.num_channels  :
                                                  sizeof (uint8_t)  * format.num_channels;

      fseek  (pImageFile,                                 0, SEEK_SET  );
      fread  (_scratchMemory.get (), _.getInitialSize (), 1, pImageFile);
      rewind (pImageFile);

      jxlDecoderSetInput   (jxl_decoder, _scratchMemory.get (), _.getInitialSize ());
      jxlDecoderCloseInput (jxl_decoder);

      for (;;)
      {
        JxlDecoderStatus status =
          jxlDecoderProcessInput (jxl_decoder);

        if (status == JXL_DEC_ERROR)
        {
          PLOG_ERROR << "Decoder error";
          break;
        }

        else if (status == JXL_DEC_NEED_MORE_INPUT)
        {
          PLOG_ERROR << "Error, already provided all input";
          break;
        }

        else if (status == JXL_DEC_BASIC_INFO)
        {
          if (JXL_DEC_SUCCESS != jxlDecoderGetBasicInfo (jxl_decoder, &info))
          {
            PLOG_ERROR << "JxlDecoderGetBasicInfo failed";
            break;
          }

          image.width  = static_cast <float> (info.xsize);
          image.height = static_cast <float> (info.ysize);

          jxlResizableParallelRunnerSetThreads ( jxl_runner,
            std::max (8U, jxlResizableParallelRunnerSuggestThreads (info.xsize, info.ysize)) );
        }

        else if (status == JXL_DEC_COLOR_ENCODING)
        {
          // Its own color space (before asking for scRGB): an encoded one (libjxl's enums match cICP's numbers), or
          //   else an ICC profile
          JxlColorEncoding original = { };
          image.color_source.origin = "JPEG XL";

          if (JXL_DEC_SUCCESS == jxlDecoderGetColorAsEncodedProfile (jxl_decoder, JXL_COLOR_PROFILE_TARGET_ORIGINAL, &original) &&
              original.color_space != JXL_COLOR_SPACE_XYB && original.color_space != JXL_COLOR_SPACE_UNKNOWN)
          {
            image.color_source.has_cicp   = true;
            image.color_source.full_range = 1;
            image.color_source.primaries  =
              (original.color_space == JXL_COLOR_SPACE_GRAY)                                    ?  1 : // Gray: Rec. 709 white
              (original.primaries   == JXL_PRIMARIES_P3 && original.white_point == JXL_WHITE_POINT_D65) ? 12 : // Display P3
              (original.primaries   == JXL_PRIMARIES_CUSTOM)                                    ?  2 :
                                                  static_cast <uint8_t> (original.primaries);
            image.color_source.transfer   =
              (original.transfer_function == JXL_TRANSFER_FUNCTION_GAMMA) ? 2 : static_cast <uint8_t> (original.transfer_function);

            if (original.transfer_function == JXL_TRANSFER_FUNCTION_GAMMA)
              image.color_source.gamma = static_cast <uint32_t> (original.gamma * 100000.0 + 0.5);
          }

          else if (jxlDecoderGetICCProfileSize != nullptr && jxlDecoderGetColorAsICCProfile != nullptr)
          {
            size_t icc_size = 0;

            if (JXL_DEC_SUCCESS == jxlDecoderGetICCProfileSize (jxl_decoder, JXL_COLOR_PROFILE_TARGET_ORIGINAL, &icc_size) && icc_size > 0)
            {
              std::vector <uint8_t> icc (icc_size);

              if (JXL_DEC_SUCCESS == jxlDecoderGetColorAsICCProfile (jxl_decoder, JXL_COLOR_PROFILE_TARGET_ORIGINAL, icc.data (), icc.size ()))
                _SetICC (image.color_source, icc.data (), icc.size ());
            }
          }

          static constexpr JxlColorEncoding
            scrgb_encoding = { .color_space       = JXL_COLOR_SPACE_RGB,
                               .white_point       = JXL_WHITE_POINT_D65,
                               .primaries         = JXL_PRIMARIES_SRGB,
                               .transfer_function = JXL_TRANSFER_FUNCTION_LINEAR,
                               .rendering_intent  = JXL_RENDERING_INTENT_PERCEPTUAL };

          if ( JXL_DEC_SUCCESS !=
                 jxlDecoderSetPreferredColorProfile (jxl_decoder, &scrgb_encoding) )
          {
            PLOG_ERROR << "JxlDecoderSetPreferredColorProfile failed";
          }

          if ( JXL_DEC_SUCCESS !=
                 jxlDecoderGetColorAsEncodedProfile (jxl_decoder, JXL_COLOR_PROFILE_TARGET_DATA, &actual_encoding) )
          {
            PLOG_ERROR << "jxlDecoderGetColorAsEncodedProfile failed";
          }
        }

        else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER)
        {
          size_t buffer_size;
          if ( JXL_DEC_SUCCESS !=
                 jxlDecoderImageOutBufferSize (jxl_decoder, &format, &buffer_size) )
          {
            PLOG_ERROR << "JxlDecoderImageOutBufferSize failed";
            break;
          }

          if (buffer_size != image.width * image.height * bpp)
          {
            PLOG_ERROR << "Invalid out buffer size " << buffer_size << " " << (int)image.width * (int)image.height * bpp;
            break;
          }

          if (SUCCEEDED (img.Initialize2D (DXGI_FORMAT_R32G32B32A32_FLOAT, static_cast <size_t> (image.width),
                                                                           static_cast <size_t> (image.height), 1, 1)))
          {
            image.bpc      = info.bits_per_sample;
            image.channels = info.num_color_channels;

            void*  pixels_buffer      = static_cast <void *>(img.GetPixels ());
            size_t pixels_buffer_size = img.GetPixelsSize ();

            std::ignore = jxlDecoderSetImageOutBitDepth;

            if (JXL_DEC_SUCCESS != jxlDecoderSetImageOutBuffer(jxl_decoder, &format,
                                                               pixels_buffer,
                                                               pixels_buffer_size))
            {
              PLOG_ERROR << "JxlDecoderSetImageOutBuffer failed";
              break;
            }
          }
        }

        else if (status == JXL_DEC_FULL_IMAGE)
        {
          // Nothing to do. Do not yet return. If the image is an animation, more
          // full frames may be decoded. This example only keeps the last one.
        }

        else if (status == JXL_DEC_SUCCESS)
        {
          succeeded = true;

          meta.format    = DXGI_FORMAT_R16G16B16A16_FLOAT;
          meta.width     = static_cast <size_t> (image.width);
          meta.height    = static_cast <size_t> (image.height);
          meta.depth     = 1;
          meta.arraySize = 1;
          meta.mipLevels = 1;
          meta.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

          bool wcg = false;
          bool hdr = false;

          using namespace DirectX;

          ScratchImage converted_img;

          const XMVECTOR vRelativeToAbsoluteNits =
            XMVectorReplicate (info.intensity_target != 255.0f ? info.intensity_target / 80.0f
                                                               : 1.0f);

          const bool bIsHDR10 =
            actual_encoding.primaries         == JXL_PRIMARIES_2100 &&
            actual_encoding.transfer_function == JXL_TRANSFER_FUNCTION_PQ;

          const bool bIsRec709Linear =
            actual_encoding.primaries         == JXL_PRIMARIES_SRGB &&
            actual_encoding.transfer_function == JXL_TRANSFER_FUNCTION_LINEAR;

          // HLG (BT.2100), as libjxl hands it over unconverted (all but XYB images): decoded for a 1,000-nit display
          //   (_HLGToDisplay), then its primaries (Rec. 2020, Rec. 709 or P3) converted to scRGB's
          const bool bIsHLG =
            actual_encoding.transfer_function == JXL_TRANSFER_FUNCTION_HLG &&
           (actual_encoding.primaries         == JXL_PRIMARIES_2100 ||
            actual_encoding.primaries         == JXL_PRIMARIES_SRGB ||
            actual_encoding.primaries         == JXL_PRIMARIES_P3);

          const uint8_t hlgPrimaries = (actual_encoding.primaries == JXL_PRIMARIES_SRGB) ?  1 :
                                       (actual_encoding.primaries == JXL_PRIMARIES_P3)   ? 12 : 9;
          const float*  hlgWeights   = _LuminanceWeights (hlgPrimaries);

          PLOG_INFO_IF (bIsHLG) << "HLG JPEG XL image: decoded as for a 1,000-nit display";

          if (actual_encoding.white_point != JXL_WHITE_POINT_D65)
          {
            PLOG_WARNING << "Unexpected non-D65 white point";
          }

          // libjxl converts only XYB encoded images (lossy ones) into the scRGB asked for, even with its color management
          //   set (libjxl 0.10): any other comes as it is encoded (sRGB, Display P3...). Those are converted here (with
          //     Little CMS, see color_convert.inl), as they were otherwise taken as scRGB (shown washed out, and of the
          //       wrong colors).
          bool bConvertedHere = false;

          if (! (bIsHDR10 || bIsRec709Linear || bIsHLG))
          {
            cmsHPROFILE           source = _CreateSDRSourceProfile (image.color_source);
            const DirectX::Image* data   = img.GetImage (0, 0, 0);

            if (source != nullptr && data != nullptr)
              bConvertedHere =
                _ConvertToScRGB (source, TYPE_RGBA_FLT, data->pixels, data->rowPitch, data->pixels, data->rowPitch,
                                 static_cast <uint32_t> (data->width), static_cast <uint32_t> (data->height), TYPE_RGBA_FLT);

            if (source != nullptr)
              cmsCloseProfile (source);

            if (bConvertedHere)
              PLOG_INFO    << "Converted the JPEG XL image's colors (" << _DescribeColorSource (image.color_source) << ") to scRGB";
            else
              PLOG_WARNING << "Encoded image is neither HDR10 nor scRGB, and could not be converted ("
                           << _DescribeColorSource (image.color_source) << ")";
          }

          if ( SUCCEEDED ( TransformImage (*img.GetImages (),
                [&](      XMVECTOR* outPixels,
                    const XMVECTOR* inPixels,
                          size_t    width,
                          size_t    y)
                {
                  UNREFERENCED_PARAMETER(y);

                  for (size_t j = 0; j < width; ++j)
                  {
                    XMVECTOR v = inPixels [j];
                    float vW = XMVectorGetW(v);

                    if (bIsRec709Linear || bConvertedHere)
                    {
                      v =
                        XMVectorMultiply (v, vRelativeToAbsoluteNits);
                    }

                    else if (bIsHDR10)
                    {
                      v =
                        XMVector3Transform (SKIV_Image_PQToLinear (v), c_Bt2100toscRGB);
                    }

                    else if (bIsHLG)
                    {
                      const float signal [3] = { XMVectorGetX (v), XMVectorGetY (v), XMVectorGetZ (v) };
                      float       light  [3];
                      _HLGToDisplay (signal, hlgWeights, light);

                      const XMVECTOR l = XMVectorSet (light [0], light [1], light [2], 0.0f);

                      v = (hlgPrimaries == 1)  ? XMVectorScale (l, 125.0f) :
                          (hlgPrimaries == 12) ? XMVectorScale (XMVector3Transform (l, c_fromDCIP3to709), 125.0f)
                                               : XMVector3Transform (l, c_Bt2100toscRGB);
                    }

                    // Outside of sRGB: a channel below 0 (past rounding). Above SDR white: its luminance over 1.0, as a
                    //   channel over 1.0 is also what colors outside of sRGB have (Display P3 red is 1.22 in scRGB).
                    static const XMVECTOR vRounding  = XMVectorReplicate (-0.0001f);
                    static const XMVECTOR vLuminance = XMVectorSet (0.2126f, 0.7152f, 0.0722f, 0.0f);

                    uint32_t xm_test_rec709 = 0x0;

                    if (XMVectorGreaterOrEqualR (&xm_test_rec709, XMVectorSetW (v, 1.0f), vRounding);
                        XMComparisonAnyFalse    ( xm_test_rec709))
                    {
                      wcg = true;
                    }

                    if (XMVectorGetX (XMVector3Dot (v, vLuminance)) > 1.001f)
                    {
                      hdr = true;
                    }

                    outPixels [j] = XMVectorSetW (v, vW);
                  }
                }, converted_img )
              )
            )
          {
            DirectX::Convert (*converted_img.GetImages (), DXGI_FORMAT_R16G16B16A16_FLOAT, DirectX::TEX_FILTER_DEFAULT, 0.0f, img);
          }

          // HDR only if it says so, or goes above SDR white; SDR images of colors outside of sRGB (below 0) are wide gamut
          //   SDR ones, as images of other formats converted to scRGB are (see color_convert.inl)
          const dynamic_range_s range =
            _JXLDynamicRange (image.color_source, info.intensity_target, wcg, hdr);

          image.light_info.isHDR = range.hdr;
          image.is_hdr           = range.hdr;
          image.is_wcg           = range.wcg;

          break;
        }

        else
        {
          PLOG_ERROR << "Unknown decoder status";
          break;
        }
      }
    }

    if (jxl_decoder != nullptr)
      jxlDecoderDestroy (jxl_decoder);

    if ( jxl_runner != nullptr)
      jxlResizableParallelRunnerDestroy (nullptr);
  }
#endif

  // Push the existing texture to a stack to be released after the frame
  //   Do this regardless of whether we could actually load the new cover or not
  if (image.pRawTexSRV.p != nullptr)
  {
    extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;
    PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << image.pRawTexSRV.p << " to be released";;
    SKIF_ResourcesToFree.push (image.pRawTexSRV.p);
    image.pRawTexSRV.p = nullptr;
  }

  if (! succeeded)
    return false;

  // SDR images whose colors are not sRGB (wide gamut: Display P3, Adobe RGB...; see color_convert.inl): converted to scRGB
  //   (linear, unclamped half floats), which keeps their colors outside of sRGB. Drawn as any other SDR image is (the
  //     shaders take its values as linear, and keep the sign of those below 0), and made sRGB again to be copied or saved.
  //       Only the decoders that leave the colors as they are in the file: JPEG XL converts them itself, and the others
  //         are HDR or of fixed colors.
  if (! image.is_hdr && (decoder == ImageDecoder_WIC  || decoder == ImageDecoder_stbi ||
                         decoder == ImageDecoder_WebP || decoder == ImageDecoder_AVIF) &&
      DirectX::FormatDataType (img.GetMetadata ().format) == DirectX::FORMAT_TYPE_UNORM) // Not ones already converted (AVIF of 10+ bpc)
  {
    if (cmsHPROFILE source = _CreateSourceProfile (image.color_source))
    {
      const DXGI_FORMAT format = img.GetMetadata ().format;

      const cmsUInt32Number lcmsFormat =
        (format == DXGI_FORMAT_R8G8B8A8_UNORM     || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) ? TYPE_RGBA_8  :
        (format == DXGI_FORMAT_B8G8R8A8_UNORM     || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
         format == DXGI_FORMAT_B8G8R8X8_UNORM     || format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB) ? TYPE_BGRA_8  :
        (format == DXGI_FORMAT_R16G16B16A16_UNORM)                                              ? TYPE_RGBA_16 : 0;

      const DirectX::Image* in = img.GetImage (0, 0, 0);
      DirectX::ScratchImage wide;
      const DWORD           start = SKIF_Util_timeGetTime1 ();

      if (lcmsFormat != 0 && in != nullptr &&
          SUCCEEDED (wide.Initialize2D (DXGI_FORMAT_R16G16B16A16_FLOAT, in->width, in->height, 1, 1)) &&
          _ConvertToScRGB (source, lcmsFormat, in->pixels, in->rowPitch, wide.GetPixels (), wide.GetImage (0, 0, 0)->rowPitch,
                           static_cast <uint32_t> (in->width), static_cast <uint32_t> (in->height)))
      {
        // No alpha (X8): opaque
        if (format == DXGI_FORMAT_B8G8R8X8_UNORM || format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
        {
          const DirectX::Image* out = wide.GetImage (0, 0, 0);

          for (size_t y = 0; y < out->height; y++)
            for (size_t x = 0; x < out->width; x++)
              reinterpret_cast <uint16_t*> (out->pixels + y * out->rowPitch) [x * 4 + 3] = 0x3C00; // 1.0
        }

        std::swap (img, wide);
        meta         = img.GetMetadata ();
        image.is_wcg = true;

        PLOG_INFO << "[Image Processing] Converted the image's colors (" << _DescribeColorSource (image.color_source)
                  << ") to scRGB in " << (SKIF_Util_timeGetTime1 () - start) << " ms.";
      }

      else
      {
        PLOG_WARNING << "[Image Processing] Could not convert the image's colors (" << _DescribeColorSource (image.color_source)
                     << ", format " << static_cast <int> (format) << "): shown as sRGB";

        if (DirectX::MakeSRGB (format) != format && img.OverrideFormat (DirectX::MakeSRGB (format)))
          meta.format = DirectX::MakeSRGB (format);
      }

      cmsCloseProfile (source);
    }
  }

  DirectX::ScratchImage* pImg  =   &img;
  DirectX::ScratchImage   converted_img;

  // We don't want single-channel icons, so convert to RGBA
  if (meta.format == DXGI_FORMAT_R8_UNORM)
  {
    if (SUCCEEDED (DirectX::Convert (pImg->GetImages(), pImg->GetImageCount(), pImg->GetMetadata (), DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, 0.0f, converted_img)))
    {
      meta =  converted_img.GetMetadata ();
      pImg = &converted_img;
    }
  }

#if 0
  // Downscale covers to 220x330, which will then be shown in horizon mode
  if (false)
  {
    size_t width  = 220;
    size_t height = 330;

    if (
      SUCCEEDED (
        DirectX::Resize (
          pImg->GetImages   (), pImg->GetImageCount (),
          pImg->GetMetadata (), width, height,
          DirectX::TEX_FILTER_FANT,
              converted_img
        )
      )
    )
    {
      meta =  converted_img.GetMetadata ();
      pImg = &converted_img;
    }
  }
#endif

  auto pDevice =
    SKIF_D3D11_GetDevice ();

  if (! pDevice)
    return false;

  pRawTex2D        = nullptr;

  succeeded = false;

  // Light levels and gamut coverage: of HDR images, and of wide gamut SDR ones (scRGB as well, see color_convert.inl)
  if (image.is_hdr || image.is_wcg)
  {
    using namespace DirectX;

    assert (meta.format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
            meta.format == DXGI_FORMAT_R32G32B32A32_FLOAT);

    XMVECTOR vMaxCLL   =   g_XMZero;
    float    fMaxLum   =       0.0f;
    float    fMinLum   = 5240320.0f;
    float    fMaxLum99 =       0.0f;

    auto        luminance_freq = std::make_unique <uint32_t []> (65536);
    ZeroMemory (luminance_freq.get (),     sizeof (uint32_t)  *  65536);

    double dLumAccum = 0.0;

    static constexpr float FLT16_MIN = 0.0000000894069671630859375f;

    EvaluateImage ( pImg->GetImages     (),
                    pImg->GetImageCount (),
                    pImg->GetMetadata   (),
    [&](const XMVECTOR* pixels, size_t width, size_t y)
    {
      UNREFERENCED_PARAMETER(y);

      XMVECTOR vColorXYZ;
      XMVECTOR vColorDCIP3;
      XMVECTOR vColor2020;
      XMVECTOR vColorAP1;
      XMVECTOR vColorAP0;
      XMVECTOR v;

      uint32_t xm_test_all = 0x0;

      double dScanlineLum = 0.0;

      for (size_t j = 0; j < width; ++j)
      {
        v = *pixels;

        vMaxCLL =
          XMVectorMax (v, vMaxCLL);

        vColorXYZ =
          XMVector3Transform (v, c_from709toXYZ);

        xm_test_all = 0x0;

        #define FP16_MIN 0.0005f

        if (XMVectorGreaterOrEqualR (&xm_test_all, v, g_XMZero);
            XMComparisonAllTrue     ( xm_test_all) || XMVectorGetY (vColorXYZ) < FP16_MIN)
        {
          image.colorimetry.pixel_counts.rec_709++;
        }

        else
        {
          vColorDCIP3 =
            XMVector3Transform (v, c_from709toDCIP3);

          if (XMVectorGreaterOrEqualR (&xm_test_all, vColorDCIP3, g_XMZero);
              XMComparisonAnyFalse    ( xm_test_all))
          {
            vColor2020 =
              XMVector3Transform (v, c_from709to2020);

            if (XMVectorGreaterOrEqualR (&xm_test_all, vColor2020, g_XMZero);
                XMComparisonAnyFalse    ( xm_test_all))
            {
              vColorAP1 =
                XMVector3Transform (v, c_from709toAP1);

              if (XMVectorGreaterOrEqualR (&xm_test_all, vColorAP1, g_XMZero);
                  XMComparisonAnyFalse    ( xm_test_all))
              {
                vColorAP0 =
                  XMVector3Transform (v, c_from709toAP0);

                if (XMVectorGreaterOrEqualR (&xm_test_all, vColorAP0, g_XMZero);
                    XMComparisonAnyFalse    ( xm_test_all))
                {
                  image.colorimetry.pixel_counts.undefined++;
                }

                else
                {
                  image.colorimetry.pixel_counts.ap0++;
                }
              }

              else
              {
                image.colorimetry.pixel_counts.ap1++;
              }
            }

            else
            {
              image.colorimetry.pixel_counts.rec_2020++;
            }
          }

          else
          {
            image.colorimetry.pixel_counts.dci_p3++;
          }
        }

        image.colorimetry.pixel_counts.total++;

        const float fLum =
          XMVectorGetY (vColorXYZ);

        fMaxLum =
          std::max (fMaxLum, fLum);
        fMinLum =
          std::min (fMinLum, fLum);

        dScanlineLum +=
          std::max (0.0, static_cast <double> (fLum));

        pixels++;
      }

      dLumAccum +=
        (dScanlineLum / static_cast <float> (width));
    } );

    float fMaxLumActual =           fMaxLum;
    float fMinLumActual = std::max (fMinLum, 0.0f);

    // 0 nits - 10k nits (appropriate for screencap, but not HDR photography)
    fMinLum = std::clamp (fMinLum, 0.0f,    125.0f);
    fMaxLum = std::clamp (fMaxLum, fMinLum, 125.0f);

    const float fLumRange =
            (fMaxLum - fMinLum);

    EvaluateImage ( pImg->GetImages     (),
                    pImg->GetImageCount (),
                    pImg->GetMetadata   (),
    [&](const XMVECTOR* pixels, size_t width, size_t y)
    {
      UNREFERENCED_PARAMETER(y);

      for (size_t j = 0; j < width; ++j)
      {
        XMVECTOR v = *pixels++;

        v =
          XMVectorMax (g_XMZero, XMVector3Transform (v, c_from709toXYZ));

        luminance_freq [
          std::clamp ( (int)
            std::roundf (
              (XMVectorGetY (v) - fMinLum) /
                                    (fLumRange / 65536.0f) ),
                                              0, 65535 ) ]++;
      }
    });

          double percent  = 100.0;
    const double img_size = (double)pImg->GetMetadata ().width *
                            (double)pImg->GetMetadata ().height;

    for (auto i = 65535; i >= 0; --i)
    {
      percent -=
        100.0 * ((double)luminance_freq [i] / img_size);

      if (percent <= 99.94)
      {
        PLOG_INFO << "99.94th percentile luminance: " <<
          80.0f * (fMinLum + (fLumRange * ((float)i / 65536.0f)))
                                                      << " nits";

        fMaxLum99 =
            fMinLum + (fLumRange * ((float)i / 65536.0f));

        break;
      }
    }

    const float fMaxCLL =
      std::max ({
        XMVectorGetX (vMaxCLL),
        XMVectorGetY (vMaxCLL),
        XMVectorGetZ (vMaxCLL)
      });

    XMVECTOR vMaxCLLReplicated =
      XMVectorReplicate (fMaxCLL);

    char cMaxChannel =
      fMaxCLL == XMVectorGetX (vMaxCLL) ? 'R' :
      fMaxCLL == XMVectorGetY (vMaxCLL) ? 'G' :
      fMaxCLL == XMVectorGetZ (vMaxCLL) ? 'B' :
                                          'X';

    // Use the maximum luminance if for some reason the percentile calc failed
    if (fMaxLum99 <= 0.01f)
        fMaxLum99  = fMaxLum;

    image.light_info.max_cll      = fMaxCLL;
    image.light_info.max_cll_name = cMaxChannel;
    image.light_info.max_nits     = std::max (0.0f, fMaxLumActual * 80.0f); // scRGB
    image.light_info.min_nits     = std::max (0.0f, fMinLumActual * 80.0f); // scRGB
    image.light_info.p99_nits     = std::max (0.0f, fMaxLum99     * 80.0f); // scRGB

    // We use the sum of averages per-scanline to help avoid overflow
    image.light_info.avg_nits     = static_cast <float> (80.0 *
      (dLumAccum / static_cast <double> (meta.height)));
  }

  HRESULT hr =
    DirectX::CreateTexture (pDevice, pImg->GetImages (), pImg->GetImageCount (), meta, (ID3D11Resource **)&pRawTex2D.p);

  if (SUCCEEDED (hr))
  {
    // The gamut coverage chart (drawn into by the shaders, in the Gamut Coverage visualization)
    if (image.is_hdr || image.is_wcg)
    {
      D3D11_TEXTURE2D_DESC
        texDesc            = { };
        texDesc.BindFlags  = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        texDesc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texDesc.SampleDesc = { .Count   = 1,
                               .Quality = 0 };
        texDesc.Usage      = D3D11_USAGE_DEFAULT;
        texDesc.ArraySize  = 1;
        texDesc.MipLevels  = 1; // The shader only ever writes to the top level (0 would allocate a full mip chain)
        texDesc.Width      = 1024;
        texDesc.Height     = 1024;

      // It is cleared on the main thread, as the image is taken (SKIV_ImageStore::TakeResult): not here, as the immediate
      //   context is not thread-safe, and the main thread renders with it meanwhile
      if (SUCCEEDED (pDevice->CreateTexture2D (&texDesc, nullptr, &pGamutCoverageTex2D.p)))
      {
        pDevice->CreateUnorderedAccessView (pGamutCoverageTex2D.p, nullptr, &image.pGamutCoverageUAV.p);
        pDevice->CreateShaderResourceView  (pGamutCoverageTex2D.p, nullptr, &image.pGamutCoverageSRV.p);
      }
    }

    // Remember HDR images read using the WIC encoder (not SDR ones converted to scRGB, above)
    if ((meta.format == DXGI_FORMAT_R16G16B16A16_FLOAT  ||
         meta.format == DXGI_FORMAT_R32G32B32A32_FLOAT) && decoder == ImageDecoder_WIC && ! image.is_wcg)
      image.light_info.isHDR = true;

    D3D11_SHADER_RESOURCE_VIEW_DESC
    srv_desc                           = { };
    srv_desc.Format                    = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels       = UINT_MAX;
    srv_desc.Texture2D.MostDetailedMip =  0;

    if (pRawTex2D.p != nullptr && SUCCEEDED (pDevice->CreateShaderResourceView (pRawTex2D.p, &srv_desc, &image.pRawTexSRV.p)))
    {
      DWORD post = SKIF_Util_timeGetTime1 ( );
      PLOG_INFO << "[Image Processing] Processed image in " << (post - pre) << " ms.";

      // Update the image width/height
      image.width  = static_cast<float>(meta.width);
      image.height = static_cast<float>(meta.height);

      succeeded = true;
    }

    // SRV is holding a reference, this is not needed anymore.
    pRawTex2D           = nullptr;
    pGamutCoverageTex2D = nullptr;
  }

  return succeeded;
};

#pragma endregion

bool
SKIV_DecodeImage (const SKIV_DecodeRequest& request, image_s& image, std::vector <ImGuiToast>& notices)
{
  image.file_info.path      = request.path;
  image.file_info.path_utf8 = SK_WideCharToUTF8 (request.path);
  image.file_info.size      = SK_File_GetSize   (request.path.c_str ());

  return
    _DecodeImage (image, request, notices);
}

#pragma region Colors

// The color code of the decoding (color_source.inl, color_convert.inl), for the viewer

std::string SKIV_DescribeColorSource         (const color_source_s& cs) { return _DescribeColorSource         (cs); }
std::string SKIV_DescribeColorSourceInDetail (const color_source_s& cs) { return _DescribeColorSourceInDetail (cs); }

// Saves a wide gamut SDR image (scRGB, as captured with SKIV_CaptureImage's scRGB) in its own colors: those of the file
//   it was loaded from (its ICC profile, or one made of its cICP, Exif...), of 8 bpc, the profile embedded in the file:
//     PNG (iCCP) and JPEG (APP2). Other formats (WIC cannot embed one), or colors with no profile: sRGB, the colors outside
//       of it clipped (SKIV_Image_SaveToDisk_SDR).
HRESULT
SKIV_Image_SaveWideGamutSDR (const DirectX::Image& scrgb, const wchar_t* path, const color_source_s& cs)
{
  const wchar_t* ext  = PathFindExtensionW (path);
  const bool     png  = (ext != nullptr && _wcsicmp (ext, L".png")  == 0),
                 jpeg = (ext != nullptr && (_wcsicmp (ext, L".jpg") == 0 || _wcsicmp (ext, L".jpeg") == 0));

  std::unique_ptr <void, decltype (&cmsCloseProfile)> target (
    (png || jpeg) ? _CreateSourceProfile (cs) : nullptr, &cmsCloseProfile);

  auto _AsSRGB = [&](void) -> HRESULT
  {
    DirectX::ScratchImage srgb;
    HRESULT hr = DirectX::Convert (scrgb, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DirectX::TEX_FILTER_DEFAULT, 0.0f, srgb);
    return FAILED (hr) ? hr : SKIV_Image_SaveToDisk_SDR (*srgb.GetImages ( ), path, false);
  };

  if (target == nullptr)
  {
    PLOG_INFO << "Saving the wide gamut image as sRGB (" << ((png || jpeg) ? "its colors have no profile" : "the format cannot have an ICC profile") << ")";
    return _AsSRGB ( );
  }

  // The profile in the file: the file's own, if its colors are those of its ICC profile; otherwise one made of them
  std::vector <uint8_t> icc;

  if (! cs.icc.empty ( ) && ! cs.icc_has_cicp && ! (cs.has_cicp && cs.cicp_first))
    icc = cs.icc;

  else
  {
    // Its description: its colors, as the details panel names them ("Display P3, sRGB", "Adobe RGB (1998)", ...)
    std::string name = _DescribeColorSource (cs);

    if      (name.rfind ("cICP: ", 0) == 0)            name = name.substr (6);
    else if (name.find  ("Adobe RGB") != name.npos)     name = "Adobe RGB (1998)";

    if (cmsMLU* desc = cmsMLUalloc (nullptr, 1))
    {
      cmsMLUsetASCII (desc, "en", "US", name.c_str ( ));
      cmsWriteTag    (target.get ( ), cmsSigProfileDescriptionTag, desc);
      cmsMLUfree     (desc);
    }

    cmsUInt32Number size = 0;

    if (cmsSaveProfileToMem (target.get ( ), nullptr, &size) && size > 0)
    {
      icc.resize (size);
      cmsSaveProfileToMem (target.get ( ), icc.data ( ), &size);
    }
  }

  // scRGB to its colors, 8 bpc
  DirectX::ScratchImage encoded;
  cmsHPROFILE           scrgbProfile = _CreateScRGBProfile ( );
  cmsHTRANSFORM         transform    = (scrgbProfile == nullptr) ? nullptr :
    cmsCreateTransform (scrgbProfile, TYPE_RGBA_HALF_FLT, target.get ( ), TYPE_RGBA_8, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOCACHE);

  if (scrgbProfile != nullptr)
    cmsCloseProfile (scrgbProfile);

  if (transform == nullptr || icc.empty ( ) || scrgb.format != DXGI_FORMAT_R16G16B16A16_FLOAT ||
      FAILED (encoded.Initialize2D (DXGI_FORMAT_R8G8B8A8_UNORM, scrgb.width, scrgb.height, 1, 1)))
  {
    if (transform != nullptr)
      cmsDeleteTransform (transform);

    PLOG_WARNING << "Could not convert the wide gamut image to its colors: saving it as sRGB";
    return _AsSRGB ( );
  }

  const DirectX::Image* out = encoded.GetImage (0, 0, 0);

  _ForRows (static_cast <uint32_t> (scrgb.height), [&](uint32_t first, uint32_t last)
  {
    for (uint32_t y = first; y < last; y++)
      cmsDoTransform (transform, scrgb.pixels + y * scrgb.rowPitch, out->pixels + y * out->rowPitch, static_cast <cmsUInt32Number> (scrgb.width));
  });

  cmsDeleteTransform (transform);

  // Written by WIC (as sRGB), then given its profile instead
  HRESULT hr = SKIV_Image_SaveToDisk_SDR (*out, path, false);

  if (FAILED (hr))
    return hr;

  std::vector <uint8_t> file;
  {
    std::ifstream stream (std::filesystem::path (path), std::ios::binary);
    file.assign ((std::istreambuf_iterator <char> (stream)), std::istreambuf_iterator <char> ( ));
  }

  if (! (png ? _EmbedICCInPNG (file, icc) : _EmbedICCInJPEG (file, icc)))
  {
    PLOG_WARNING << "Could not embed the ICC profile: saving the wide gamut image as sRGB";
    return _AsSRGB ( );
  }

  {
    std::ofstream stream (std::filesystem::path (path), std::ios::binary | std::ios::trunc);
    stream.write (reinterpret_cast <const char*> (file.data ( )), static_cast <std::streamsize> (file.size ( )));

    if (! stream)
      return E_FAIL;
  }

  PLOG_INFO << "Saved the wide gamut image in its own colors (" << _DescribeColorSource (cs) << "), with an ICC profile of "
            << icc.size ( ) << " bytes";
  return S_OK;
}

#pragma endregion
