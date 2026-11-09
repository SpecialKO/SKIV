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

#include <tabs/viewer.h>
#include "../../version.h"

#include <wmsdk.h>
#include <filesystem>
#include <SKIV.h>
#include <utility/utility.h>
#include <utility/skif_imgui.h>
#include <ImGuiNotify.hpp>

#include "DirectXTex/DirectXTex.h"
#include <wincodec.h>
#include <dxgi1_4.h> // IDXGIAdapter3::QueryVideoMemoryInfo (animated images)

#ifdef OpenEXR
#ifdef _M_X64
#include <package_misc/DirectXTexEXR.h>
#endif
#endif

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>

#include <cwctype>
#include <regex>
#include <iostream>
#include <locale>
#include <codecvt>
#include <fstream>
#include <string>
#include <sstream>
#include <concurrent_queue.h>
#include <strsafe.h>
#include <atlimage.h>
#include <TlHelp32.h>

#include <utility/fsutil.h>
#include <utility/registry.h>
#include <utility/updater.h>
#include <utility/sk_utility.h>

#include <imgui/imgui_impl_dx11.h>

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

#include <html_coder.hpp>
#include <utility/image.h>

#pragma comment (lib, "dxguid.lib")

ImRect copyRect = { 0,0,0,0 };
bool wantCopyToClipboard = false;

thread_local stbi__context::iccp_s SKIV_STBI_ICCP;
thread_local bool                  SKIV_STBI_srgb;
thread_local stbi__context::cicp_s SKIV_STBI_CICP;
thread_local stbi__context::sbit_s SKIV_STBI_SBIT;
thread_local stbi__result_info     SKIV_STBI_ResultInfo;

float SKIV_HDR_SDRWhite = 80.0f;

float SKIV_HDR_GamutHue_Rec709    [4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // White
float SKIV_HDR_GamutHue_DciP3     [4] = { 0.0f, 1.0f, 1.0f, 1.0f }; // Cyan
float SKIV_HDR_GamutHue_Rec2020   [4] = { 0.0f, 1.0f, 0.0f, 1.0f }; // Green
float SKIV_HDR_GamutHue_Ap1       [4] = { 1.0f, 1.0f, 0.0f, 1.0f }; // Yellow
float SKIV_HDR_GamutHue_Ap0       [4] = { 1.0f, 0.0f, 1.0f, 1.0f }; // Magenta
float SKIV_HDR_GamutHue_Undefined [4] = { 1.0f, 0.0f, 0.0f, 1.0f }; // Red

uint32_t SKIV_HDR_VisualizationId       = SKIV_HDR_VISUALIZTION_NONE;
uint32_t SKIV_HDR_VisualizationFlagsSDR = 0xFFFFFFFFU;
float    SKIV_HDR_MaxCLL                =   1.0f;
float    SKIV_HDR_MaxLuminance          =  80.0f;
float    SKIV_HDR_DisplayMaxLuminance   =   0.0f;
float    SKIV_HDR_BrightnessScale       = 100.0f;

float    SKIV_HDR_MaxLuminanceP99       =  80.0f;

CComPtr <ID3D11UnorderedAccessView>
         SKIV_HDR_GamutCoverageUAV      = nullptr;
CComPtr <ID3D11ShaderResourceView>
         SKIV_HDR_GamutCoverageSRV      = nullptr;

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

const std::initializer_list<FileSignature> supported_sdr_encode_formats =
{
  FileSignature { L"image/png",                 { L".png"  },          { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A } },
  FileSignature { L"image/jpeg",                { L".jpg", L".jpeg" }, { 0xFF, 0xD8, 0x00, 0x00 },   // JPEG (SOI; Start of Image)
                                                                       { 0xFF, 0xFF, 0x00, 0x00 } }, // JPEG App Markers are masked as they can be all over the place (e.g. 0xFF 0xD8 0xFF 0xED)
  FileSignature { L"image/vnd.ms-photo",        { L".jxr",  L".hdp" }, { 0x49, 0x49, 0xBC } },
  FileSignature { L"image/bmp",                 { L".bmp"  },          { 0x42, 0x4D } },
  FileSignature { L"image/tiff",                { L".tiff", L".tif" }, { 0x49, 0x49, 0x2A, 0x00 } }, // TIFF: little-endian
  FileSignature { L"image/tiff",                { L".tiff", L".tif" }, { 0x4D, 0x4D, 0x00, 0x2A } }, // TIFF: big-endian
  FileSignature { L"image/avif",                { L".avif" },          { 0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x61, 0x76, 0x69, 0x66 } },   // ftypavif
//FileSignature { L"image/vnd-ms.dds",          { L".dds"  },          { 0x44, 0x44, 0x53, 0x20 } },
//FileSignature { L"image/x-targa",             { L".tga"  },          { 0x00, } }, // TGA has no real unique header identifier, so just use the file extension on those
};

const std::initializer_list<FileSignature> supported_hdr_encode_formats =
{
  FileSignature { L"image/png",                 { L".png"  },          { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A } },
  FileSignature { L"image/vnd.ms-photo",        { L".jxr"  },          { 0x49, 0x49, 0xBC } },
  FileSignature { L"image/vnd.radiance",        { L".hdr"  },          { 0x23, 0x3F, 0x52, 0x41, 0x44, 0x49, 0x41, 0x4E, 0x43, 0x45, 0x0A } }, // Radiance High Dynamic Range image file
#ifdef _M_X64
  FileSignature { L"image/x-exr",               { L".exr"  },          { 0x76, 0x2F, 0x31, 0x01 } },
#endif
  FileSignature { L"image/jxl",                 { L".jxl"  },          { 0xFF, 0x0A } },
  FileSignature { L"image/avif",                { L".avif" },          { 0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x61, 0x76, 0x69, 0x66 },   // ftypavif
                                                                       { 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } }, // ?? ?? ?? ?? 66 74 79 70 61 76 69 66
//FileSignature { L"image/vnd-ms.dds",          { L".dds"  },          { 0x44, 0x44, 0x53, 0x20 } },
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



struct filterspec_s {
  std::list<std::pair<std::wstring, std::wstring>> _raw_list  = { };
  std::vector<COMDLG_FILTERSPEC>                   filterSpec = { };
};

enum eFILTERSPEC
{
  Open,
  Save_SDR,
  Save_HDR
};

static filterspec_s
CreateFILTERSPEC (eFILTERSPEC format)
{
  filterspec_s
    _spec = { };

  const std::initializer_list<FileSignature>*
    _formats = ((format == Open    ) ? &supported_formats
             :  (format == Save_SDR) ? &supported_sdr_encode_formats
             :                         &supported_hdr_encode_formats);


  { // All supported formats
    std::wstring ext_filter;

    for (auto& type : *_formats)
    {
      static std::wstring prev_mime;
      std::wstring mime = type.mime_type;

      if (mime == prev_mime)
        continue;

      for (auto& file_extension : type.file_extensions)
        ext_filter += ((! ext_filter.empty()) ? L";*" : L"*") + file_extension;

      prev_mime = mime;
    }

    _spec._raw_list.push_back ({ L"All supported formats", ext_filter });
  }

  for (auto& type : *_formats)
  {
    static std::wstring prev_mime;
    std::wstring mime = type.mime_type;

    if (mime == prev_mime)
      continue;

    std::wstring ext_filter;
    for (auto& file_extension : type.file_extensions)
      ext_filter += ((! ext_filter.empty()) ? L";*" : L"*") + file_extension;

    _spec._raw_list.push_back ({ type.mime_type, ext_filter });

    prev_mime = mime;
  }

  _spec.filterSpec       = std::vector<COMDLG_FILTERSPEC> (_spec._raw_list.size());
  COMDLG_FILTERSPEC* ptr = _spec.filterSpec.data();

  for (const auto& filter : _spec._raw_list)
  {
    ptr->pszName = filter.first.c_str();
    ptr->pszSpec = filter.second.c_str();
    ++ptr;
  }

  return _spec;
};

bool                   loadImage         = false;
bool                   tryingToLoadImage = false; // Loading image...
bool                   tryingToDownImage = false; // Downloading image...
std::atomic<bool>      imageLoading      = false;
bool                   resetScrollCenter = false;
bool                   newImageLoaded    = false; // Set by the window msg handler when a new image has been loaded
bool                   newImageFailed    = false; // Set by the window msg handler when a new image failed to load
bool                   imageFailWarning  = false; // Set to true to warn about a failed image load

bool                   activateSnipping  = false; // Set to true when a desktop capture is complete and ready to snip
bool                   iconicBeforeSnip  = false;
bool                   trayedBeforeSnip  = false;
HWND                   hwndBeforeSnip    =  0;
HWND                   hwndTopBeforeSnip =  0; // Window above SKIV in z-order
//ImRect               selection_rect    = { };

bool                   coverRefresh      = false; // This just triggers a refresh of the cover
std::wstring           coverRefreshPath  = L"";
int                    coverRefreshCount = 0;
int                    numRegular        = 0;
int                    numPinnedOnTop    = 0;

std::wstring           defaultHDRFileExt = L".png";
std::wstring           defaultSDRFileExt = L".png";

const float fTintMin     = 0.75f;
      float fTint        = 1.0f;
      float fAlpha       = 0.0f;
      float fAlphaPrev   = 1.0f;

PopupState OpenFileDialog  = PopupState_Closed;
PopupState SaveFileDialog  = PopupState_Closed;
PopupState ExportSDRDialog = PopupState_Closed;
PopupState ContextMenu     = PopupState_Closed;
PopupState ConfigEncoders  = PopupState_Closed;

// The frames of an animated image (APNG, GIF, WebP), and where it is in playing them (_LoadAnimatedImage).
//   Frames that fit share a few textures (pages), each in a cell of its own: a texture per frame takes much more video
//     memory than its pixels, as drivers place textures in pages of video memory (as SKIF learned with its animated covers).
//       Each cell has a one-pixel border repeating the frame's edges, so filtering never takes in a neighboring frame.
struct image_anim_s {
  struct frame_s {
    CComPtr <ID3D11ShaderResourceView> srv;                    // Its texture (shared with the other frames of its page)
    ImVec2                             uv0    = ImVec2 (0, 0); // Its part of the texture
    ImVec2                             uv1    = ImVec2 (1, 1);
    ImVec2                             offset = ImVec2 (0, 0); // ... in pixels (its top left corner)
  };

  std::vector <frame_s> frames;
  std::vector <DWORD>   frameEnds;                        // When each frame ends (ms into the animation)
  DWORD                 duration     = 0;                 // ms
  ImVec2                resolution   = ImVec2 (0, 0);     // The frames' size (smaller than the image if it had to be scaled down)
  size_t                textures     = 0;                 // How many textures the frames take
  size_t                bytes        = 0;                 //   ... and how much video memory

  // Playback (main thread only)
  DWORD                 playingSince = 0;                 // 0: not playing (paused, or not started yet)
  DWORD                 position     = 0;                 // ms into the animation when it was last paused / started
  bool                  paused       = false;             // Paused by the user
  size_t                current      = 0;                 // The frame shown last

  DWORD  getPosition     (DWORD now) const;               // ms into the animation
  size_t getFrameAt      (DWORD position) const;
  DWORD  getFrameStart   (size_t frame) const { return (frame == 0 || frame > frameEnds.size ( )) ? 0 : frameEnds [frame - 1]; }
  void   play            (DWORD now);
  void   pause           (DWORD now);
  void   step            (DWORD now, int frames);         // Pauses, and moves this many frames ahead (or back)
};

DWORD
image_anim_s::getPosition (DWORD now) const
{
  if (duration == 0)
    return 0;

  return (playingSince != 0) ? (position + (now - playingSince)) % duration
                             :  position                         % duration;
}

size_t
image_anim_s::getFrameAt (DWORD pos) const
{
  const auto frameEnd = std::upper_bound (frameEnds.begin ( ), frameEnds.end ( ), pos);
  const auto frame    = static_cast <size_t> (std::distance (frameEnds.begin ( ), frameEnd));

  return std::min (frame, frames.size ( ) - 1);
}

void
image_anim_s::play (DWORD now)
{
  paused = false;

  if (playingSince == 0)
    playingSince = now;
}

void
image_anim_s::pause (DWORD now)
{
  paused = true;

  if (playingSince != 0)
  {
    position     = getPosition (now);
    playingSince = 0;
  }
}

void
image_anim_s::step (DWORD now, int count)
{
  pause (now);

  if (frames.empty ( ))
    return;

  const int size  = static_cast <int> (frames.size ( ));
  const int frame = ((static_cast <int> (getFrameAt (position)) + count) % size + size) % size;

  position = getFrameStart (static_cast <size_t> (frame));
}

struct image_s {
  struct file_s {
    std::wstring filename         = { }; // Image filename
    std:: string filename_utf8    = { };
    std::wstring folder_path      = { }; // Parent folder path
    std:: string folder_path_utf8 = { };
    std::wstring path             = { }; // Image path (full)
    std:: string path_utf8        = { };
    uint64_t     size             = 0;
    file_s ( ) { };
  } file_info;

  int          bpc         =    0;
  int          channels    =    0;
  float        width       = 0.0f;
  float        height      = 0.0f;
  static float zoom;//     = 1.0f; // 1.0f = 100%; max: 5.0f; min: 0.05f
  static
  ImageScaling scaling     ;//= ImageScaling_Auto;
  ImVec2       uv0 = ImVec2 (0, 0);
  ImVec2       uv1 = ImVec2 (1, 1);
  ImVec2       avail_size;        // Holds the frame size used (affected by the scaling method)
  ImVec2       avail_size_cache;  // Holds a cached value used to determine if avail_size needs recalculating

  bool         not_hdr     = false; //just an excessive bool for checks
  bool         is_hdr      = false;
  bool         is_wcg      = false; // SDR, of colors outside of sRGB: converted to scRGB (see color_convert.inl)
  bool         is_dds      = false;

  CComPtr <ID3D11ShaderResourceView>  pRawTexSRV;
  CComPtr <ID3D11UnorderedAccessView> pGamutCoverageUAV;
  CComPtr <ID3D11ShaderResourceView>  pGamutCoverageSRV;

  // Animated images: their frames (pRawTexSRV is then the first frame's texture, holding a reference of its own).
  //   Released with SKIV_ReleaseAnimation, like the textures above are pushed to SKIF_ResourcesToFree.
  std::shared_ptr <image_anim_s>      anim;

  // How the file describes its colors (ICC profile, cICP, ...), see color_source.inl
  color_source_s color_source;

  struct light_info_s {
    float max_cll      = 0.0f;
    char  max_cll_name =  '?';
    float max_nits     = 0.0f;
    float min_nits     = 0.0f;
    float avg_nits     = 0.0f;
    float p99_nits     = 0.0f;
    bool  isHDR        = false;
  } light_info;

  struct gamut_info_s {
    struct pixel_samples_s {
      uint32_t rec_709;
      uint32_t rec_2020;
      uint32_t dci_p3;
      uint32_t ap1;
      uint32_t ap0;
      uint32_t undefined;
      uint32_t total;

      float getPercentRec709    (void) const;
      float getPercentRec2020   (void) const;
      float getPercentDCIP3     (void) const;
      float getPercentAP1       (void) const;
      float getPercentAP0       (void) const;
      float getPercentUndefined (void) const;
    } pixel_counts;
  } colorimetry;

  // copy assignment
  image_s& operator= (const image_s other) noexcept
  {
    file_info           = other.file_info;
    bpc                 = other.bpc;
    channels            = other.channels;
    width               = other.width;
    height              = other.height;
    zoom                = other.zoom;
    scaling             = other.scaling;
    uv0                 = other.uv0;
    uv1                 = other.uv1;
    avail_size          = other.avail_size;
    avail_size_cache    = other.avail_size_cache;
    pRawTexSRV.p        = other.pRawTexSRV.p;
    pGamutCoverageSRV.p = other.pGamutCoverageSRV.p;
    pGamutCoverageUAV.p = other.pGamutCoverageUAV.p;
    anim                = other.anim;
    color_source        = other.color_source;
    not_hdr             = other.not_hdr;
    is_hdr              = other.is_hdr;
    is_wcg              = other.is_wcg;
    is_dds              = other.is_dds;
    light_info          = other.light_info;
    colorimetry         = other.colorimetry;
    return *this;
  }

  void reset (void)
  {
    file_info           = { };
    bpc                 =    0;
    channels            =    0;
    width               = 0.0f;
    height              = 0.0f;
  //zoom                = 1.0f;
  //scaling             = ImageScaling_Auto;
    uv0                 = ImVec2 (0, 0);
    uv1                 = ImVec2 (1, 1);
    avail_size          = { };
    avail_size_cache    = { };
    pRawTexSRV.p        = nullptr;
    pGamutCoverageSRV.p = nullptr;
    pGamutCoverageUAV.p = nullptr;
    anim                = nullptr; // Not released: whoever was given it (cover_old) releases it
    color_source        = { };
    is_hdr              = false;
    is_wcg              = false;
    is_dds              = false;
    light_info          = { };
    colorimetry         = { };
  }
};

float        image_s::zoom    = 1.0f;
ImageScaling image_s::scaling = ImageScaling_Auto;

float
image_s::gamut_info_s::pixel_samples_s::getPercentRec709 (void) const
{
  return (total == 0 || rec_709 == 0) ? 0.0f : 100.0f *
    static_cast <float> ( static_cast <double> (rec_709) /
                          static_cast <double> (total) );
}

float
image_s::gamut_info_s::pixel_samples_s::getPercentRec2020 (void) const
{
  return (total == 0 || rec_2020 == 0) ? 0.0f : 100.0f *
    static_cast <float> ( static_cast <double> (rec_2020) /
                          static_cast <double> (total)  );
};

float
image_s::gamut_info_s::pixel_samples_s::getPercentDCIP3 (void) const
{
  return (total == 0 || dci_p3 == 0) ? 0.0f : 100.0f *
    static_cast <float> ( static_cast <double> (dci_p3) /
                          static_cast <double> (total)  );
};

float
image_s::gamut_info_s::pixel_samples_s::getPercentAP1 (void) const
{
  return (total == 0 || ap1 == 0) ? 0.0f : 100.0f *
    static_cast <float> ( static_cast <double> (ap1) /
                          static_cast <double> (total) );
};

float
image_s::gamut_info_s::pixel_samples_s::getPercentAP0 (void) const
{
  return (total == 0 || ap0 == 0) ? 0.0f : 100.0f *
    static_cast <float> ( static_cast <double> (ap0) /
                          static_cast <double> (total) );
};

float
image_s::gamut_info_s::pixel_samples_s::getPercentUndefined (void) const
{
  return (total == 0 || undefined == 0) ? 0.0f : 100.0f *
    static_cast <float> ( static_cast <double> (undefined) /
                          static_cast <double> (total) );
};

std::wstring dragDroppedFilePath = L"";

extern bool            allowShortcutCtrlA;
extern ImVec2          SKIF_vecAlteredSize;
extern float           SKIF_ImGui_GlobalDPIScale;
extern std::string     SKIF_StatusBarHelp;
extern std::string     SKIF_StatusBarText;
extern DWORD           invalidatedDevice;
extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;

// Drops this reference to an animated image's frames. The last one pushes their textures to be released after the frame
//   (SKIF_ResourcesToFree), as they may still be drawn in it.
static void
SKIV_ReleaseAnimation (std::shared_ptr <image_anim_s>& anim)
{
  if (anim == nullptr)
    return;

  if (anim.use_count ( ) == 1)
  {
    for (auto& frame : anim->frames)
      if (frame.srv.p != nullptr)
        SKIF_ResourcesToFree.push (frame.srv.Detach ( ));

    PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushed the " << anim->frames.size ( ) << " frames of an animated image to be released";
  }

  anim.reset ( );
}

// Animated images, for the main loop (SKIV.cpp): when the frame shown changes, the window is presented again even though
//   nothing else changed, and while one plays, the loop wakes up when its next frame is due.
static DWORD SKIV_Viewer_AnimNextFrameAt = 0;     // When the next frame is due (SKIF_Util_timeGetTime1); 0: none
static int   SKIV_Viewer_AnimDrawnFrame  = -1;    // The last frame (ImGui::GetFrameCount) an animation was drawn in
static bool  SKIV_Viewer_AnimChanged     = false; // The frame shown changed since the window was last presented

// ms until the next frame of the animation shown is due (0: now); INFINITE if none plays
DWORD
SKIV_Viewer_GetAnimationWakeIn (void)
{
  if (SKIV_Viewer_AnimNextFrameAt == 0 || ImGui::GetFrameCount ( ) - SKIV_Viewer_AnimDrawnFrame > 1)
    return INFINITE;

  if (SKIF_ImGui_hWnd == NULL || ::IsIconic (SKIF_ImGui_hWnd))
    return INFINITE;

  const LONG wakeIn = static_cast <LONG> (SKIV_Viewer_AnimNextFrameAt - SKIF_Util_timeGetTime1 ( ));

  return (wakeIn > 0) ? static_cast <DWORD> (wakeIn) : 0;
}

// Whether the frame of the animation shown changed since this was last asked (the window must be presented again)
bool
SKIV_Viewer_ConsumeAnimationChange (void)
{
  return std::exchange (SKIV_Viewer_AnimChanged, false);
}

// The texture to draw an image with, and its part of it: an animated image's current frame (which starts playing the
//   first time it is drawn), or the image's own texture. track: the shown image (cover), which wakes the main loop.
static ID3D11ShaderResourceView*
SKIV_GetImageFrame (image_s& image, ImVec2& uv0, ImVec2& uv1, bool track)
{
  uv0 = image.uv0;
  uv1 = image.uv1;

  if (image.anim == nullptr || image.anim->frames.empty ( ))
    return image.pRawTexSRV.p;

  image_anim_s& anim = *image.anim;
  const DWORD   now  = SKIF_Util_timeGetTime ( );

  if (! anim.paused && anim.playingSince == 0)
    anim.play (now);

  const DWORD  position = anim.getPosition (now);
  const size_t frame    = anim.getFrameAt  (position);

  if (track)
  {
    if (frame != anim.current)
      SKIV_Viewer_AnimChanged = true;

    SKIV_Viewer_AnimDrawnFrame  = ImGui::GetFrameCount ( );
    SKIV_Viewer_AnimNextFrameAt = (anim.playingSince != 0)
                                ? std::max (SKIF_Util_timeGetTime1 ( ) + (anim.frameEnds [frame] - position), 1UL)
                                : 0;
  }

  anim.current = frame;

  uv0 = anim.frames [frame].uv0;
  uv1 = anim.frames [frame].uv1;

  return anim.frames [frame].srv.p;
}

// The pixels of the image shown, read back from the GPU: an animated image's current frame (at the size its frames
//   were made at), or the whole image
static HRESULT
SKIV_CaptureImage (image_s& image, DirectX::ScratchImage& captured)
{
  extern CComPtr <ID3D11Device> SKIF_D3D11_GetDevice (bool bWait = true);

  auto pDevice =
    SKIF_D3D11_GetDevice ();

  if (pDevice == nullptr || image.pRawTexSRV.p == nullptr)
    return E_POINTER;

  CComPtr <ID3D11DeviceContext>  pDevCtx;
  pDevice->GetImmediateContext (&pDevCtx.p);

  if (pDevCtx.p == nullptr)
    return E_POINTER;

  ImVec2 uv0, uv1;
  ID3D11ShaderResourceView* pSRV =
    SKIV_GetImageFrame (image, uv0, uv1, false);

  CComPtr <ID3D11Resource> pTexResource;
  pSRV->GetResource (&pTexResource.p);

  if (pTexResource.p == nullptr)
    return E_POINTER;

  if (image.anim == nullptr && ! image.is_wcg)
    return DirectX::CaptureTexture (pDevice, pDevCtx, pTexResource.p, captured);

  // Wide gamut SDR images (scRGB): as sRGB again, of 8 bpc, the colors outside of it clipped, to be copied or saved as SDR
  if (image.anim == nullptr)
  {
    DirectX::ScratchImage scrgb;
    HRESULT hr =
      DirectX::CaptureTexture (pDevice, pDevCtx, pTexResource.p, scrgb);

    return FAILED (hr) ? hr :
      DirectX::Convert (*scrgb.GetImages (), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DirectX::TEX_FILTER_DEFAULT, 0.0f, captured);
  }

  DirectX::ScratchImage page;
  HRESULT hr =
    DirectX::CaptureTexture (pDevice, pDevCtx, pTexResource.p, page);

  if (FAILED (hr))
    return hr;

  const image_anim_s::frame_s& frame = image.anim->frames [image.anim->current];

  const size_t width  = static_cast <size_t> (image.anim->resolution.x),
               height = static_cast <size_t> (image.anim->resolution.y);

  if (FAILED (hr = captured.Initialize2D (page.GetMetadata ( ).format, width, height, 1, 1)))
    return hr;

  return DirectX::CopyRectangle (*page.GetImages ( ), DirectX::Rect (static_cast <size_t> (frame.offset.x), static_cast <size_t> (frame.offset.y), width, height),
                                 *captured.GetImages ( ), DirectX::TEX_FILTER_DEFAULT, 0, 0);
}

#define _WIDTH_SCROLLBAR (SKIF_vecAlteredSize.y > 0.0f ? ImGui::GetStyle().ScrollbarSize : 0.0f)
#define _WIDTH   (378.0f * SKIF_ImGui_GlobalDPIScale) - _WIDTH_SCROLLBAR // GameList, GameDetails, Injection_Summary_Frame (prev. 414.0f)
// 1038px == 415px
// 1000px == 377px (using 380px)

std::atomic<int>  textureLoadQueueLength{ 1 };

static int getTextureLoadQueuePos (void) {
  return textureLoadQueueLength.fetch_add(1) + 1;
}

// External declaration
extern void SKIF_Shell_AddJumpList     (std::wstring name, std::wstring path, std::wstring parameters, std::wstring directory, std::wstring icon_path, bool bService);

// Functions / Structs

static float
AdjustAlpha (float a)
{
  return std::pow (a, 1.0f / 2.2f );
}

#pragma region SaveTempImage

static bool
SaveTempImage (std::wstring_view source, std::wstring_view filename)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  if (source.empty())
    return false;

  struct thread_s {
    std::wstring source      = L"";
    std::wstring destination = L"";
    std::wstring filename    = L"";
  };

  thread_s* data = new thread_s;

  data->source      = source;
  data->destination = _path_cache.skiv_temp;
  data->filename    = filename;

  HANDLE hWorkerThread = (HANDLE)
  _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
  {
    SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIV_ImageWorkerHTTP");

    // Is this combo really appropriate for this thread?
    SKIF_Util_SetThreadPowerThrottling (GetCurrentThread (), 1); // Enable EcoQoS for this thread
    SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

  //PLOG_DEBUG << "SKIV_ImageWorkerHTTP thread started!";

    thread_s* _data = static_cast<thread_s*>(var);

    CoInitializeEx (nullptr, 0x0);

    PLOG_INFO  << "Downloading web image asynchronously...";

    std::error_code ec;
    // Create any missing directories
    if (! std::filesystem::exists (            _data->destination, ec))
          std::filesystem::create_directories (_data->destination, ec);

    // Combine the destination folder + filename
    _data->destination += _data->filename;

    bool success = false;

    // This both downloads a new image from the internet as well as copies a local file to the destination
    // BMP files are downloaded to .tmp, while all others are downloaded to their intended path
    success = SKIF_Util_GetWebResource (_data->source, _data->destination);

    // If the file was copied successfully, we also need to ensure it's not marked as read-only
    if (success)
      SetFileAttributes (_data->destination.c_str(),
      GetFileAttributes (_data->destination.c_str()) & ~FILE_ATTRIBUTE_READONLY);

    else
    {
      PLOG_ERROR << "Could not save the source image to the destination path!";
      PLOG_ERROR << "Source:      " << _data->source;
      PLOG_ERROR << "Destination: " << _data->destination;
    }

    if (success)
    {
      // If the specified window was created by the calling thread, the window procedure is called immediately as a subroutine.
      wchar_t                    wszFilePath [MAX_PATH] = { };
      if (S_OK == StringCbCopyW (wszFilePath, MAX_PATH, _data->destination.data()))
      {
        COPYDATASTRUCT cds { };
        cds.dwData = SKIV_CDS_STRING;
        cds.lpData = &wszFilePath;
        cds.cbData = sizeof(wszFilePath);

        // If the window msg pump returns true on our WM_COPYDATA call,
        //   that means the data has been transferred over and we can free the memroy.
        if (SendMessage (SKIF_Notify_hWnd,
                      WM_COPYDATA,
                      (WPARAM) SKIF_ImGui_hWnd,
                      (LPARAM) (LPVOID) &cds))
          PLOG_VERBOSE << "Data transfer successful!";

        // Delete the temp file in case of an error
        else
          DeleteFile (_data->destination.c_str());
      }
    }

    else {
      PostMessage (SKIF_Notify_hWnd, WM_SKIF_IMAGE, 0x0, static_cast<LPARAM> (success));
      PLOG_ERROR << "Failed to process the new cover image!";
    }

    PLOG_INFO  << "Finished downloading web image asynchronously...";

    // Free up the memory we allocated
    delete _data;

  //PLOG_DEBUG << "SKIV_ImageWorkerHTTP thread stopped!";

    SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

    return 0;
  }, data, 0x0, nullptr);

  bool threadCreated = (hWorkerThread != NULL);

  if (threadCreated) // We don't care about how it goes so the handle is unneeded
    CloseHandle (hWorkerThread);
  else // Someting went wrong during thread creation, so free up the memory we allocated earlier
    delete data;

  return threadCreated;
}

#pragma endregion


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

  // The size the frames are made at: as they are, unless all of them would take more than the budget
  double scale = 1.0;

  if (const double bytes = static_cast <double> (width) * height * 4.0 * count; bytes > budget)
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
  //   WIC's filters; their textures are sRGB (_CreateAnimationTexture)
  constexpr DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

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

  // Colors that are not sRGB (wide gamut, see color_convert.inl): made sRGB, the colors outside of it clipped. The frames
  //   stay 8 bpc, as scRGB (half floats) would fit only half as many of them into the budget.
  std::unique_ptr <void, decltype (&cmsDeleteTransform)> toSRGB (nullptr, &cmsDeleteTransform);

  if (cmsHPROFILE profile = _CreateSourceProfile (image.color_source))
  {
    toSRGB.reset (_CreateSRGBTransform (profile));
    cmsCloseProfile (profile);

    PLOG_INFO_IF (toSRGB != nullptr) << "[Image Processing] The animated image's colors (" << _DescribeColorSource (image.color_source)
                                     << ") are converted to sRGB";
  }

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
        uint8_t*       d      = dst->pixels + (y + row) * dst->rowPitch + x * 4;

        memcpy (d,                    s,                        4);
        memcpy (d + 4,                s,                        outWidth * 4);
        memcpy (d + 4 + outWidth * 4, s + (outWidth - 1) * 4,   4);
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

  image.anim         = anim;
  image.pRawTexSRV   = anim->frames [0].srv; // A reference of its own
  image.uv0          = anim->frames [0].uv0;
  image.uv1          = anim->frames [0].uv1;
  image.width        = static_cast <float> (width);
  image.height       = static_cast <float> (height);
  image.bpc          = 8;
  image.channels     = 4;
  image.is_hdr       = false;
  image.is_wcg       = false; // Made sRGB, if they were not
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

static void
_LogColorSource (const image_s& image)
{
  std::string detail = _DescribeColorSourceInDetail (image.color_source);
  std::replace (detail.begin (), detail.end (), '\n', ';');

  PLOG_INFO << "Color space: " << _DescribeColorSource (image.color_source) << " [" << detail << "]";
}

bool
LoadLibraryTexture (image_s& image, const std::function <bool (void)>& wanted = nullptr)
{
  SKIV_ScopedThreadPriority_Viewer _scoped_thread_prio;

  // NOT REALLY THREAD-SAFE WHILE IT RELIES ON THESE GLOBAL OBJECTS!
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

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
  if (_registry.bAnimatedImages && image_sig != nullptr &&
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
    const bool cicp  = hdr10;

    SKIV_STBI_ICCP.iCCP = ! image.color_source.icc.empty ();

    if (image_sig->mime_type == L"image/png")
    {
      std::string_view     data_view ((const char *)_scratchMemory.get (), _.getInitialSize ());

      if (auto sbit_pos  = data_view.find ("sBIT", 0, 4);
               sbit_pos != data_view.npos)
      {
        unsigned long size =
          *((unsigned long *)&_scratchMemory.get ()[sbit_pos-4]);

#if (defined _M_IX86) || (defined _M_X64)
        size = _byteswap_ulong (size);
#endif

        memcpy (&SKIV_STBI_SBIT, &_scratchMemory.get ()[sbit_pos+4], std::min (4ul, size));
      }
    }

    float*                pixels = hdr10 ?
                                 nullptr :
                                   stbi_loadf_from_memory (_scratchMemory.get (), static_cast <int> (_.getInitialSize ()), &width, &height, &channels_in_file, desired_channels);

    // stb_image reads a cICP chunk into SKIV_STBI_CICP itself: only HDR10 is taken from it (others crashed the HDR path)
    if (hdr10)
    {
      SKIV_STBI_CICP.primaries     =  9;
      SKIV_STBI_CICP.transfer_func = 16;
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

        assert (SKIV_STBI_CICP.primaries     ==  9); // BT 2020
        assert (SKIV_STBI_CICP.transfer_func == 16); // ST 2084
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

        // Check for BT.2020 using ST.2084 (HDR10)
        if ( SKIV_STBI_CICP.primaries     ==  9 &&
             SKIV_STBI_CICP.transfer_func == 16 )
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

            PLOG_INFO << "HDR10 PNG detected, transforming to scRGB...";

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

                  XMVECTOR r =
                    XMVector3Transform (SKIV_Image_PQToLinear (v), c_Bt2100toscRGB);

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
            ImGui::InsertNotification(
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

          else if (avif_decoder->image->colorPrimaries == AVIF_COLOR_PRIMARIES_XYZ)
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
                          XMVector3Transform (SKIV_Image_PQToLinear (v), c_fromXYZto709), 125.0f
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
                          XMVector3Transform (SKIV_Image_PQToLinear (v), c_from601to709), 125.0f
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
                          XMVector3Transform (SKIV_Image_PQToLinear (v), c_fromDCIP3to709), 125.0f
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
                avif_decoder->image->transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084)
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
                        SKIV_Image_PQToLinear(v), 125.0f
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
              ImGui::InsertNotification(
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
              ImGui::InsertNotification (
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
                        XMVector3Transform (SKIV_Image_PQToLinear (v), c_Bt2100toscRGB);

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

          if (actual_encoding.white_point != JXL_WHITE_POINT_D65)
          {
            PLOG_WARNING << "Unexpected non-D65 white point";
          }

          // libjxl converts only XYB encoded images (lossy ones) into the scRGB asked for, even with its color management
          //   set (libjxl 0.10): any other comes as it is encoded (sRGB, Display P3...). Those are converted here (with
          //     Little CMS, see color_convert.inl), as they were otherwise taken as scRGB (shown washed out, and of the
          //       wrong colors).
          bool bConvertedHere = false;

          if (! (bIsHDR10 || bIsRec709Linear))
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

      if (SUCCEEDED (pDevice->CreateTexture2D (&texDesc, nullptr, &pGamutCoverageTex2D.p)))
      {
        pDevice->CreateUnorderedAccessView (pGamutCoverageTex2D.p, nullptr, &image.pGamutCoverageUAV.p);
        pDevice->CreateShaderResourceView  (pGamutCoverageTex2D.p, nullptr, &image.pGamutCoverageSRV.p);

        CComPtr <ID3D11DeviceContext>  pDevCtx;
        pDevice->GetImmediateContext (&pDevCtx);

        if (pDevCtx.p != nullptr)
        {
          FLOAT fClearColor [] = { 0.f, 0.f, 0.f, 0.f };
          pDevCtx->ClearUnorderedAccessViewFloat (image.pGamutCoverageUAV, fClearColor);
        }
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

#pragma region AspectRatio

ImVec2
GetCurrentAspectRatio (image_s& image)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static ImageScaling last_scaling = ImageScaling_Auto;

  ImVec2 avail_size = ImFloor (ImGui::GetContentRegionAvail ( ) / SKIF_ImGui_GlobalDPIScale);

  // Clear any cached data on image changes
  if (image.pRawTexSRV.p == nullptr || image.height == 0 || image.width  == 0)
    return avail_size;

  // Do not recalculate when dealing with an unchanged situation
  if (avail_size   == image.avail_size_cache &&
      last_scaling == image.scaling)
    return image.avail_size;

  image.avail_size_cache   = avail_size;

  float avail_width        = avail_size.x;
  float avail_height       = avail_size.y;
  float frameAspectRatio   = avail_width / avail_height;
  float contentAspectRatio = image.width / image.height;

  ImVec2 diff = ImVec2(0.0f, 0.0f);

  // Reset the aspect ratio before we calculate new ones
  image.uv0 = ImVec2 (0, 0);
  image.uv1 = ImVec2 (1, 1);

  ImageScaling _appliedScaling = image.scaling;

  // Attempt to find best scaling method on load
  if (_appliedScaling == ImageScaling_Auto)
  {
    // None: if smaller than window size
    if (avail_width > image.width && avail_height > image.height)
      _appliedScaling = ImageScaling_None;

    // Fit: all other scenarios
    else
      _appliedScaling = ImageScaling_Fit;
  }

  // None / 1:1 / "View actual size"
  if (_appliedScaling == ImageScaling_None)
  {
    avail_width  = image.width;
    avail_height = image.height;
  }

  // Fit / "Zoom to fit"
  else if (_appliedScaling == ImageScaling_Fit)
  {
    if (contentAspectRatio > frameAspectRatio)
      avail_height = avail_width / contentAspectRatio;
    else
      avail_width  = avail_height * contentAspectRatio;
  }

  // Fill / "Fill window"
  else if (_appliedScaling == ImageScaling_Fill)
  {
    /* We shouldn't have visible scrollbars since
       no other image viewer cares about that anyway...
    // Workaround to prevent content/frame fighting one another
    if (ImGui::GetScrollMaxY() == 0.0f)
      avail_width -= ImGui::GetStyle().ScrollbarSize;

    if (ImGui::GetScrollMaxX() == 0.0f)
      avail_height -= ImGui::GetStyle().ScrollbarSize;
    */

    // Fill the content area
    if (contentAspectRatio > frameAspectRatio)
      avail_width  = avail_height * contentAspectRatio;
    else // if (contentAspectRatio < frameAspectRatio)
      avail_height = avail_width / contentAspectRatio;
  }

#if 0
  // Stretch
  else if (_appliedScaling == ImageScaling_Stretch)
  {
    // Do nothing -- this cases the image to be stretched
  }
#endif

  // Cache the current image scaling _and_ reset the scroll center
  if (last_scaling != image.scaling)
  {
    last_scaling    = image.scaling;
    resetScrollCenter = true;
  }

  PLOG_VERBOSE_IF(! ImGui::IsAnyMouseDown()) // Suppress logging while the mouse is down (e.g. window is being resized)
               << "\n"
               << "Image scaling  : " << _appliedScaling << "\n"
               << "Content region : " << avail_width << "x" << avail_height << "\n"
               << "Image details  :\n"
               << " > resolution  : " << image.width << "x" << image.height << "\n"
               << " > coord 0,0   : " << image.uv0.x << "," << image.uv0.y  << "\n"
               << " > coord 1,1   : " << image.uv1.x << "," << image.uv1.y;

  image.avail_size       = ImFloor (ImVec2 (avail_width, avail_height));

  return image.avail_size;
}

#pragma endregion



SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
DirectX::TexMetadata     meta      = { };
DirectX::ScratchImage    img       = { };
std::wstring             new_path  = L"";
image_s                  cover     = { },
                         cover_old = { };
int              tmp_iDarkenImages = _registry.iDarkenImages;

int32_t ImGuiToast::maxAssignedId = 0;

// Whether an image is drawn by the shaders' HDR path (its HDR visualizations): HDR images, and wide gamut SDR ones while
//   their gamut coverage is shown (their only visualization; otherwise they are drawn as other SDR images are)
static bool
SKIV_Viewer_DrawAsHDR (const image_s& image)
{
  return image.light_info.isHDR ||
        (image.is_wcg && SKIV_HDR_VisualizationId == SKIV_HDR_VISUALIZTION_GAMUT);
}

uint32_t
SKIV_Viewer_CycleVisualizationModes (void)
{
  static int32_t last_toast_id = -1;

  if (cover.is_hdr || cover.is_wcg)
  {
    // Wide gamut SDR images: only gamut coverage (or none)
    if (! cover.is_hdr)
      SKIV_HDR_VisualizationId = (SKIV_HDR_VisualizationId == SKIV_HDR_VISUALIZTION_GAMUT) ? SKIV_HDR_VISUALIZTION_NONE
                                                                                           : SKIV_HDR_VISUALIZTION_GAMUT;

    else if (++SKIV_HDR_VisualizationId >= SKIV_HDR_NUM_VISUALIZTIONS)
          SKIV_HDR_VisualizationId  = SKIV_HDR_VISUALIZTION_NONE;

    ImGuiToast toast = {
      ImGuiToastType::Info, 2500,
        "HDR Visualization Mode Changed",
        "New Mode:\t%hs",
          SKIV_HDR_VisualizationId == SKIV_HDR_VISUALIZTION_NONE    ? "None" :
          SKIV_HDR_VisualizationId == SKIV_HDR_VISUALIZTION_HEATMAP ? "Luminance Heatmap" :
          SKIV_HDR_VisualizationId == SKIV_HDR_VISUALIZTION_GAMUT   ? "Gamut Coverage"    :
          SKIV_HDR_VisualizationId == SKIV_HDR_VISUALIZTION_SDR     ? "SDR Grayscale"     :
                                                                      "Invalid"
    };

    // Dismiss any previous mode change notifications that may still be visible
    if (last_toast_id >= 0)
    {
      ImGui::DismissNotificationById (last_toast_id);
    }

    last_toast_id =
      ImGui::InsertNotification (toast);
  }

  return SKIV_HDR_VisualizationId;
}

ImageScaling
SKIV_Viewer_CycleScalingModes (void)
{
  static int32_t last_toast_id = -1;

  if (++(uint8_t&)cover.scaling >= ImageScaling_MaxValue)
                  cover.scaling  = ImageScaling_Auto;

  ImGuiToast toast = {
    ImGuiToastType::Info, 2500,
      "Scaling Mode Changed",
      "New Mode:\t%hs",
        cover.scaling == ImageScaling_Auto ? "Auto"        :
        cover.scaling == ImageScaling_None ? "Actual Size" :
        cover.scaling == ImageScaling_Fit  ? "Zoom to Fit" :
        cover.scaling == ImageScaling_Fill ? "Fill Window" :
                                             "Invalid"
  };

  // Dismiss any previous mode change notifications that may still be visible
  if (last_toast_id >= 0)
  {
    ImGui::DismissNotificationById (last_toast_id);
  }

  last_toast_id =
    ImGui::InsertNotification (toast);

  return cover.scaling;
}

// Main UI function

void
SKIF_UI_Tab_DrawViewer (void)
{
  extern bool imageFadeActive;

  // Used to monitor the current image folder
  static skiv_image_directory_s _current_folder;

  // ** Move this code somewhere more sensible
  //
  // User is requesting to copy the loaded image to clipboard,
  //   let's download it back from the GPU and have some fun!
  if (wantCopyToClipboard)
  {   wantCopyToClipboard = false;

    if (cover.pRawTexSRV.p != nullptr /* && cover.is_hdr */)
    {
      {
        {
          // An animated image: its current frame
          DirectX::ScratchImage                          captured_img;
          if (SUCCEEDED (SKIV_CaptureImage (cover, captured_img)))
          {
            if (copyRect.GetArea () != 0)
            {
              // The frames of an animated image may have been scaled down
              const float scale =
                (cover.anim != nullptr && cover.width > 0.0f) ? cover.anim->resolution.x / cover.width : 1.0f;

              const size_t
                x      = static_cast <size_t> (std::max (0.0f, copyRect.Min.x       * scale)),
                y      = static_cast <size_t> (std::max (0.0f, copyRect.Min.y       * scale)),
                width  = static_cast <size_t> (std::max (0.0f, copyRect.GetWidth  () * scale)),
                height = static_cast <size_t> (std::max (0.0f, copyRect.GetHeight () * scale));

              const DirectX::Rect
                src_rect (x,y, width,height);

              DirectX::ScratchImage
                             subrect;
              if (SUCCEEDED (subrect.Initialize2D   ( captured_img.GetMetadata ().format, width, height, 1, 1)) &&
                  SUCCEEDED (DirectX::CopyRectangle (*captured_img.GetImages   (), src_rect,
                                                                                   *subrect.GetImages (), DirectX::TEX_FILTER_DEFAULT, 0, 0)))
              {

                if (SKIV_Image_CopyToClipboard (subrect.GetImages (), cover.is_hdr, { ImRect(), L"cp_rect", CaptureMode_None } ))
                {
                  ImGui::InsertNotification (
                    {
                      ImGuiToastType::Success,
                      3000,
                      "Copied area to clipboard", "%.fx%.f -> %.fx%.f",
                      copyRect.Min.x,
                      copyRect.Min.y,
                      copyRect.Max.x,
                      copyRect.Max.y
                    }
                  );

                  copyRect = { 0,0,0,0 };
                }

                else {
                  ImGui::InsertNotification (
                    {
                      ImGuiToastType::Error,
                      3000,
                      "Failed to copy area to clipboard", "Area: %.fx%.f -> %.fx%.f",
                      copyRect.Min.x,
                      copyRect.Min.y,
                      copyRect.Max.x,
                      copyRect.Max.y
                    }
                  );
                }
              }
            }

            else
            {
              if (SKIV_Image_CopyToClipboard (captured_img.GetImages (), cover.is_hdr, { ImRect(), L"cp_full", CaptureMode_None } ))
              {
                ImGui::InsertNotification (
                  {
                    ImGuiToastType::Success,
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
              }
            }
          }
        }
      }
    }
  }

  auto _SwapOutCover = [&](void) -> void
  {
    // Hide the current cover and set it up to be unloaded
    if (cover.pRawTexSRV.p != nullptr)
    {
      // If there already is an old cover, we need to push it for release
      if (cover_old.pRawTexSRV.p != nullptr)
      {
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << cover_old.pRawTexSRV.p << " to be released";;
        SKIF_ResourcesToFree.push(cover_old.pRawTexSRV.p);
        cover_old.pRawTexSRV.p = nullptr;
      }

      if (cover_old.pGamutCoverageSRV.p != nullptr)
      {
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << cover_old.pGamutCoverageSRV.p << " to be released";;
        SKIF_ResourcesToFree.push(cover_old.pGamutCoverageSRV.p);
        cover_old.pGamutCoverageSRV.p = nullptr;
      }

      if (cover_old.pGamutCoverageUAV.p != nullptr)
      {
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << cover_old.pGamutCoverageUAV.p << " to be released";;
        SKIF_ResourcesToFree.push(cover_old.pGamutCoverageUAV.p);
        cover_old.pGamutCoverageUAV.p = nullptr;
      }

      SKIV_ReleaseAnimation (cover_old.anim);

      // Set up the current one to be released
      cover_old = cover;
      cover.reset();

      fAlphaPrev          = (_registry.bCrossFade) ? fAlpha   : 0.0f;
      fAlpha              = (_registry.bCrossFade) ?   0.0f   : 1.0f;

      // Reset the title of the main app window
      if (SKIF_ImGui_hWnd != NULL)
        ::SetWindowText (SKIF_ImGui_hWnd, SKIV_WINDOW_TITLE_SHORT_W);
    }
  };

  auto _DeleteImage = [&](void) -> void
  {
    if (_current_folder.fileList.getActiveFile() == nullptr)
      return;

    //DeleteFile (_current_folder.activeFile->path.c_str());

    // TODO: Fix focus loss bug when a warning is shown...
    if (SKIF_Util_FileExplorer_DeleteFile (_current_folder.fileList.getActiveFile()->path.c_str(), true))
    {
      ImGui::InsertNotification ({ ImGuiToastType::Success, 2000, "Deleted image:", "%s", SK_WideCharToUTF8 (_current_folder.fileList.getActiveFile()->path).c_str() });

      dragDroppedFilePath = _current_folder.fileList.deleteImage ( );

      if (dragDroppedFilePath.empty())
        _SwapOutCover ();
    }
  };

#pragma region Initialization

  SK_RunOnce (fAlpha = (_registry.bCrossFade) ? 0.0f : 1.0f);

  DWORD       current_time = SKIF_Util_timeGetTime ( );

  // Load a new cover
  // ~~ Ensure we aren't already loading this cover ~~
  if (! new_path.empty() /* &&
        new_path != cover.file_info.path */) // This prevents URL images that share name from being reloaded properly, so just ignore this stuff...
  {
    loadImage = true;

    _SwapOutCover ();
  }

  // Release old cover after it has faded away, or instantly if we don't fade covers
  if (fAlphaPrev <= 0.0f)
  {
    if (cover_old.pRawTexSRV.p != nullptr)
    {
      PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << cover_old.pRawTexSRV.p << " to be released";;
      SKIF_ResourcesToFree.push(cover_old.pRawTexSRV.p);
      cover_old.pRawTexSRV.p = nullptr;
    }

    if (cover_old.pGamutCoverageSRV.p != nullptr)
    {
      PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << cover_old.pGamutCoverageSRV.p << " to be released";;
      SKIF_ResourcesToFree.push(cover_old.pGamutCoverageSRV.p);
      cover_old.pGamutCoverageSRV.p = nullptr;
    }

    if (cover_old.pGamutCoverageUAV.p != nullptr)
    {
      PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << cover_old.pGamutCoverageUAV.p << " to be released";;
      SKIF_ResourcesToFree.push(cover_old.pGamutCoverageUAV.p);
      cover_old.pGamutCoverageUAV.p = nullptr;
    }

    SKIV_ReleaseAnimation (cover_old.anim);
  }

  // Apply changes when the image changes
  if (loadImage)
    fTint = (_registry.iDarkenImages == 0) ? 1.0f : fTintMin;
  // Apply changes when the _registry.iDarkenImages var has been changed in the Settings tab
  else if (tmp_iDarkenImages != _registry.iDarkenImages)
  {
    fTint = (_registry.iDarkenImages == 0) ? 1.0f : fTintMin;

    tmp_iDarkenImages = _registry.iDarkenImages;
  }

  if (newImageFailed)
  {
    newImageFailed   = false;
    imageFailWarning = true;  // Show the failed warning label

    // Reset the title of the main app window with the image filename
    if (SKIF_ImGui_hWnd != NULL)
      ::SetWindowText (SKIF_ImGui_hWnd, SKIV_WINDOW_TITLE_SHORT_W);
  }

  else if (newImageLoaded)
  {
    newImageLoaded    = false;
    imageFailWarning  = false;
    resetScrollCenter = true;

    // Update the title of the main app window with the image filename
    if (SKIF_ImGui_hWnd != NULL)
      ::SetWindowText (SKIF_ImGui_hWnd, (cover.file_info.filename + L" - " + SKIV_WINDOW_TITLE_SHORT_W).c_str());
  }

  // Do not clear when we are loading an image (so as to not process the same folder constantly)
  if (! loadImage && ! tryingToLoadImage)
  {
    static DWORD dwLastSignaled = 0;
    static bool  bNotifyRefresh = false; // Only notify about scans caused by changes made to the folder outside of the app

    // Identify when an image has been closed
    if (cover.file_info.path.empty())
    {
      if (! _current_folder.folder_path.empty())
      {
        PLOG_VERBOSE << "cover.file_info.folder_path: " << cover.file_info.folder_path;
        PLOG_VERBOSE << "_current_folder.folder_path: " << _current_folder.folder_path;
        _current_folder.reset();
      }

      //PLOG_VERBOSE << "cover.file_info.path: " << cover.file_info.path;
    }

    // Identify when we're dealing with a whole new folder
    if (cover.file_info.folder_path != _current_folder.folder_path)
    {
      PLOG_VERBOSE << "cover.file_info.folder_path: " << cover.file_info.folder_path;
      PLOG_VERBOSE << "_current_folder.folder_path: " << _current_folder.folder_path;
      _current_folder.reset();

      std::filesystem::path path  = SKIF_Util_NormalizeFullPath (cover.file_info.path);
      _current_folder.folder_path = path.parent_path().wstring();

      PLOG_VERBOSE << "Watching the folder... " << _current_folder.folder_path;

      // This triggers a new updateFolderData() run below
      dwLastSignaled = 1;
      bNotifyRefresh = false;
    }

    // Identify when a new file from the same folder has been dropped
    if (! _current_folder.fileList.empty() && _current_folder.fileList.getActiveFile() != nullptr &&
        ! _current_folder.fileList.getActiveFile()->filename.empty() &&
          _current_folder.fileList.getActiveFile()->filename != cover.file_info.filename)
    {
      //PLOG_VERBOSE << "new file from same folder has been dropped?";
      //PLOG_DEBUG << "                          cover.file_info.filename: " << cover.file_info.filename;
      //PLOG_DEBUG << "_current_folder.fileList.getActiveFile()->filename: " << _current_folder.fileList.getActiveFile()->filename;

      _current_folder.workerThread (true);
    }

    // Identify when the folder was changed outside of the app
    //PLOG_VERBOSE << "_current_folder.watch was signaled! Delay checking the folder for another 500 ms...";
    if (_current_folder.watch.isSignaled (_current_folder.folder_path))
    {
      dwLastSignaled = SKIF_Util_timeGetTime();
      bNotifyRefresh = true;
    }

    if (dwLastSignaled != 0 && dwLastSignaled + 500 < SKIF_Util_timeGetTime())
    {
      if (! _current_folder.fileList.fileDeleted)
        _current_folder.workerThread (true);
      else
        _current_folder.fileList.fileDeleted = false;

      dwLastSignaled = 0;
    }

    int results = _current_folder.workerThread (false);
    if (results > 0)
    {
      _current_folder.fileList.updateFileIterator (cover.file_info.path);

      PLOG_VERBOSE << "cover.file_info.path: " << cover.file_info.path;
      PLOG_VERBOSE << "_current_folder.fileList.getActiveFile(): " << _current_folder.fileList.getActiveFile()->path;

      // If the selected file was changed from within _current_folder, also update dragDroppedFilePath
      if (! _current_folder.fileList.getActiveFile()->path.empty() &&
          cover.file_info.filename != _current_folder.fileList.getActiveFile()->filename)
               dragDroppedFilePath  = _current_folder.fileList.getActiveFile()->path;

      if (results == 2 && bNotifyRefresh)
        ImGui::InsertNotification ({ ImGuiToastType::Info, 5000, "Refreshed nearby images.", "" });

      bNotifyRefresh = false;
    }
  }

  // Only apply changes to the scaling method if we actually have an image loaded
  if (cover.pRawTexSRV.p != nullptr)
  {
    if (! _current_folder.fileList.getActiveFile()->path.empty() &&
        ImGui::GetKeyData (ImGuiKey_Delete)->DownDuration == 0.0f) // Delete - Delete the opened image
      _DeleteImage ();

    // These keybindings requires Ctrl to be held down
    if (ImGui::GetIO().KeyCtrl)
    {
      auto _IsHotKeyClicked = [](ImGuiKey key, ImageScaling image_scaling) {
        if (ImGui::GetKeyData (key)->DownDuration == 0.0f)
        {
          cover.scaling = (cover.scaling != image_scaling) ? image_scaling : ImageScaling_Auto;
          cover.zoom    = 1.0f;
        }
      };

      _IsHotKeyClicked (ImGuiKey_1, ImageScaling_None); // Ctrl+1 - Image Scaling: View actual size (None / 1:1)
      _IsHotKeyClicked (ImGuiKey_2, ImageScaling_Fit ); // Ctrl+2 - Image Scaling: Zoom to fit (Fit)
      _IsHotKeyClicked (ImGuiKey_0, ImageScaling_Fit ); // Ctrl+0 - Alternate hotkey
      _IsHotKeyClicked (ImGuiKey_3, ImageScaling_Fill); // Ctrl+3 - Image Scaling: Fill the window (Fill)

      if (ImGui::GetKeyData (ImGuiKey_W)->DownDuration == 0.0f) // Ctrl+W - Close the opened image
        _SwapOutCover ();
      else if (! cover.file_info.path.empty() && // Ctrl+E - Browse folder
               ImGui::GetKeyData (ImGuiKey_E)->DownDuration == 0.0f)
        SKIF_Util_FileExplorer_SelectFile (cover.file_info.path.c_str());
    }
  }

#pragma endregion

  // This allows images to be DPI-scaled on HiDPI displays up until the user uses "View actual size" to force them to appear as 100%
  ImVec2 sizeCover     = GetCurrentAspectRatio (cover)     * ((cover    .scaling == ImageScaling_None) ? 1 : SKIF_ImGui_GlobalDPIScale) * cover    .zoom;
  ImVec2 sizeCover_old = GetCurrentAspectRatio (cover_old) * ((cover_old.scaling == ImageScaling_None) ? 1 : SKIF_ImGui_GlobalDPIScale) * cover_old.zoom;

  // From now on ImGui UI calls starts being made...

#pragma region GameCover

  static const ImVec2 hdr_uv0  (-1024.0f, -1024.0f);
  static const ImVec2 hdr_uv1  (-2048.0f, -2048.0f);
  static const ImVec2 srgb_uv0 (-4096.0f, -4096.0f);
  static const ImVec2 srgb_uv1 (-8192.0f, -8192.0f);

  static int    queuePosGameCover  = 0;
  static char   cstrLabelDowning[] = "Downloading...";
  static char   cstrLabelLoading[] = "...";
  static char   cstrLabelFailed [] = "The image failed to load... :(\n"
                                     "  Maybe try another image?";
  static char   cstrLabelMissing[] = "Drop an image...";
  char*         pcstrLabel     = nullptr;
  bool          isImageHovered = false;

  ImVec2 originalPos    = ImGui::GetCursorPos ( );
         originalPos.x -= 1.0f * SKIF_ImGui_GlobalDPIScale;

  // A new cover is meant to be loaded, so don't do anything for now...
  if (loadImage)
  { }

  else if (tryingToDownImage)
    pcstrLabel = cstrLabelDowning;

  else if (tryingToLoadImage)
    pcstrLabel = cstrLabelLoading;

  else if (imageFailWarning)
    pcstrLabel = cstrLabelFailed;

  else if (textureLoadQueueLength.load() == queuePosGameCover && cover.pRawTexSRV.p == nullptr)
    pcstrLabel = cstrLabelMissing;

  else if (cover    .pRawTexSRV.p == nullptr &&
           cover_old.pRawTexSRV.p == nullptr)
    pcstrLabel = cstrLabelMissing;

  if (pcstrLabel != nullptr)
  {
    ImVec2 labelSize = ImGui::CalcTextSize (pcstrLabel);
    ImGui::SetCursorPos (ImVec2 (
      ImGui::GetWindowSize ( ).x / 2 - labelSize.x / 2 + ImGui::GetScrollX ( ),
      ImGui::GetWindowSize ( ).y / 2 - labelSize.y / 2 + ImGui::GetScrollY ( )));
    ImGui::TextDisabled (pcstrLabel);
  }

  ImGui::SetCursorPos (originalPos);

  float fGammaCorrectedTint =
    ((! _registry._RendererHDREnabled && _registry.iSDRMode == 2) ||
      ( _registry._RendererHDREnabled && _registry.iHDRMode == 2))
        ? AdjustAlpha (fTint)
        : fTint;

  bool bIsHDR =
    cover_old.is_hdr;

  SKIV_HDR_MaxCLL       = cover_old.light_info.max_cll;
  SKIV_HDR_MaxLuminance = cover_old.light_info.max_nits;

  if (_registry._RendererHDREnabled)
    SKIV_HDR_MaxCLL = 1.0f;

  bool fading = false;

  // Display previous fading out cover
  if (cover_old.pRawTexSRV.p != nullptr && fAlphaPrev > 0.0f)
  {
    if (sizeCover_old.x < ImGui::GetContentRegionAvail().x)
      ImGui::SetCursorPosX (ImFloor ((ImGui::GetContentRegionAvail().x - sizeCover_old.x) * 0.5f));
    if (sizeCover_old.y < ImGui::GetContentRegionAvail().y)
      ImGui::SetCursorPosY (ImFloor ((ImGui::GetContentRegionAvail().y - sizeCover_old.y) * 0.5f));

    sizeCover_old = ImFloor (sizeCover_old);

    // An animated image: its current frame
    ImVec2 frame_uv0, frame_uv1;
    ID3D11ShaderResourceView* pFrameSRV =
      SKIV_GetImageFrame (cover_old, frame_uv0, frame_uv1, false);

    SKIF_ImGui_OptImage  (pFrameSRV,
                                                      ImVec2 (sizeCover_old.x,
                                                              sizeCover_old.y),
                                    SKIV_Viewer_DrawAsHDR (cover_old) ? hdr_uv0 : cover_old.is_dds ? srgb_uv0 : frame_uv0, // Top Left coordinates
                                    SKIV_Viewer_DrawAsHDR (cover_old) ? hdr_uv1 : cover_old.is_dds ? srgb_uv1 : frame_uv1, // Bottom Right coordinates
                                    (_registry._StyleLightMode) ? ImVec4 (1.0f, 1.0f, 1.0f, fGammaCorrectedTint * AdjustAlpha (fAlphaPrev))  : ImVec4 (fTint, fTint, fTint, fAlphaPrev) // Alpha transparency
    );

    ImGui::SetCursorPos (originalPos);

    fading = true;
  }

  bIsHDR =
    cover.is_hdr || cover.is_wcg; // Both have a gamut coverage chart

  if (bIsHDR && (! fading))
  {
    SKIV_HDR_GamutCoverageUAV = cover.pGamutCoverageUAV;
    SKIV_HDR_GamutCoverageSRV = cover.pGamutCoverageSRV;
  }

  else
  {
    SKIV_HDR_GamutCoverageUAV = nullptr;
    SKIV_HDR_GamutCoverageSRV = nullptr;
  }

  SKIV_HDR_MaxCLL          = cover.light_info.max_cll;
  SKIV_HDR_MaxLuminanceP99 = cover.light_info.p99_nits;
  SKIV_HDR_MaxLuminance    = cover.light_info.max_nits;

  if (sizeCover.x < ImGui::GetContentRegionAvail().x)
    ImGui::SetCursorPosX (ImFloor ((ImGui::GetContentRegionAvail().x - sizeCover.x) * 0.5f));
  if (sizeCover.y < ImGui::GetContentRegionAvail().y)
    ImGui::SetCursorPosY (ImFloor ((ImGui::GetContentRegionAvail().y - sizeCover.y) * 0.5f));

  if (_registry._RendererHDREnabled)
    SKIV_HDR_MaxCLL = 1.0f;

  sizeCover         = ImFloor (sizeCover);
  ImVec2 image_pos  = ImFloor (ImGui::GetCursorScreenPos ( )); // NOTE! Actual screen position (since that's what ImGui::Image uses)
  ImRect image_rect = ImRect (image_pos, image_pos + sizeCover);

  // An animated image: its current frame (this also keeps the main loop waking up for its next one)
  SKIV_Viewer_AnimNextFrameAt = 0;

  ImVec2 frame_uv0, frame_uv1;
  ID3D11ShaderResourceView* pFrameSRV =
    SKIV_GetImageFrame (cover, frame_uv0, frame_uv1, true);

  SKIF_ImGui_OptImage  (pFrameSRV,
                                                    ImVec2 (sizeCover.x,
                                                            sizeCover.y),
                                  SKIV_Viewer_DrawAsHDR (cover) ? hdr_uv0 : cover.is_dds ? srgb_uv0 : frame_uv0, // Top Left coordinates
                                  SKIV_Viewer_DrawAsHDR (cover) ? hdr_uv1 : cover.is_dds ? srgb_uv1 : frame_uv1, // Bottom Right coordinates
                                  (_registry._StyleLightMode) ? ImVec4 (1.0f, 1.0f, 1.0f, fGammaCorrectedTint * AdjustAlpha (fAlpha))  :
                                                                ImVec4 (1.f, 1.f, 1.f, 1.f) // Alpha transparency (2024-01-01, removed fGammaCorrectedTint * fAlpha for the light style)
  );

  isImageHovered = ImGui::IsItemHovered();

  // Reset scroll (center-align the scroll)
  if (resetScrollCenter && cover_old.pRawTexSRV.p == nullptr)
  {
    PLOG_VERBOSE << "Attempted to reset scroll...";

    resetScrollCenter = false;

    ImGui::SetScrollHereY ( );
    ImGui::SetScrollHereX ( );
  }

  if (cover.pRawTexSRV.p != nullptr)
  {
    auto& io =
      ImGui::GetIO ();

    bool         changed_zoom = false;
    static float last_zoom    = cover.zoom;

    static constexpr float ZOOM_MAX = 4.975f;
    static constexpr float ZOOM_MIN = 0.075f;

    // Using 4.975f and 0.075f to work around some floating point shenanigans
    if (     io.MouseWheel > 0 && cover.zoom < ZOOM_MAX && io.KeyCtrl)
    {
      cover.zoom += 0.05f;
    }

    else if (io.MouseWheel < 0 && cover.zoom > ZOOM_MIN && io.KeyCtrl)
    {
      cover.zoom -= 0.05f;
    }

    if (ImGui::IsKeyDown (ImGuiKey_GamepadL2))
    {
      float delta =
        ImGui::GetKeyData (ImGuiKey_GamepadLStickUp  )->AnalogValue >  0.00f ?
        ImGui::GetKeyData (ImGuiKey_GamepadLStickUp  )->AnalogValue *  0.05f :
        ImGui::GetKeyData (ImGuiKey_GamepadLStickDown)->AnalogValue >  0.00f ?
        ImGui::GetKeyData (ImGuiKey_GamepadLStickDown)->AnalogValue * -0.05f : 0.0f;

      if (delta != 0.0f)
      {
        cover.zoom =
          std::clamp (cover.zoom + 0.1f * delta, ZOOM_MIN, ZOOM_MAX);
      }
    }

    if (last_zoom != cover.zoom)
    {
      changed_zoom = true;
      last_zoom    = cover.zoom;
    }

    if (changed_zoom)
    {
      static int32_t last_toast_id = -1;

      ImGuiToast toast = {
        ImGuiToastType::Info, 500,
          cover.zoom == 1.0f ? "Zoom Level Reset"
                             : "Zoom Level Changed",
          "New Scale:\t%3.0f%%", cover.zoom * 100.0f
      };

      if (last_toast_id >= 0)
      {
        // Dismiss any previous scaling change notifications that may still be visible
        ImGui::DismissNotificationById (last_toast_id);
      }

      last_toast_id =
        ImGui::InsertNotification (toast);
    }

    static ImRect selection_rect = ImRect ();

    if ((io.KeyCtrl && SKIF_ImGui_SelectionRect (&selection_rect, image_rect)))
    {
      // Flip an inverted rectangle
      if (selection_rect.Min.x > selection_rect.Max.x) std::swap (selection_rect.Min.x, selection_rect.Max.x);
      if (selection_rect.Min.y > selection_rect.Max.y) std::swap (selection_rect.Min.y, selection_rect.Max.y);

      // Adjust for image position
      selection_rect.Min -= image_pos;
      selection_rect.Max -= image_pos;

      // Translate to image coordinates
      ImRect translated = selection_rect;
      float scale     = (sizeCover.x != cover.width) ? cover.width / sizeCover.x : 1.0f;
      translated.Min *= scale;
      translated.Max *= scale;

      /*
      ImGui::InsertNotification (
        {
          ImGuiToastType::Info,
          3000,
          "Selection", "Mouse: %.fx%.f -> %.fx%.f\nImage: %.fx%.f -> %.fx%.f",
          selection_rect.Min.x,
          selection_rect.Min.y,
          selection_rect.Max.x,
          selection_rect.Max.y,
          translated.Min.x,
          translated.Min.y,
          translated.Max.x,
          translated.Max.y
        }
      );
      */

      selection_rect = ImRect (); // Reset
      wantCopyToClipboard = true;
      copyRect            = translated;
    }
  }

#pragma endregion

#pragma region ImageDetails

  if (cover.pRawTexSRV.p != nullptr && _registry.bImageDetails)
  {
    auto parent_pos =
      ImGui::GetCursorPos ();

    // Display "floating" in the top left corner regardless of scroll position
    ImGui::SetCursorPos   (ImVec2 (ImGui::GetScrollX ( ), ImGui::GetScrollY ( )));

    ImGui::PushStyleColor (ImGuiCol_ChildBg, ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg));
    ImGui::BeginChild     ("###ImageDetails", ImVec2 (0, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ((_registry.bUIBorders) ? ImGuiChildFlags_Borders : ImGuiChildFlags_None), ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor  ( );

    // Monospace font
    ImGui::PushFont       (fontConsolas, SKIF_ImGui_GetFontConsolasSize ( ));

    float posXvalues = 150.0f * SKIF_ImGui_GlobalDPIScale;

    { // Basic File Details
      char     szLabels      [512] = { };
      char     szLabelsData  [512] = { };

      // Developer Mode
      if (_registry.bDeveloperMode)
      {
        sprintf (szLabels,     "Image:\n"
                               "Folder:\n"
                               "File Size:");
        sprintf (szLabelsData, "%s\n"
                               "%s\n"
                               "%4.2f %s", cover.file_info.filename_utf8.c_str(),
                                           cover.file_info.folder_path_utf8.c_str(),
                                           cover.file_info.size > 1024             ?
                                           cover.file_info.size > 1024*1024        ?
                      static_cast <float> (cover.file_info.size)/(1024.0f*1024.0f) :
                      static_cast <float> (cover.file_info.size)/(1024.0f)         :
                      static_cast <float> (cover.file_info.size),
                                           cover.file_info.size > 1024             ?
                                           cover.file_info.size > 1024*1024        ?
                                                                             "MiB" :
                                                                             "KiB" :
                                                                             "Bytes");
      }

      // Basic
      else {
        sprintf (szLabels,     "Image:\n"
                               "File Size:");
        sprintf (szLabelsData, "%s\n"
                               "%4.2f %s", cover.file_info.filename_utf8.c_str(),
                                           cover.file_info.size > 1024             ?
                                           cover.file_info.size > 1024*1024        ?
                      static_cast <float> (cover.file_info.size)/(1024.0f*1024.0f) :
                      static_cast <float> (cover.file_info.size)/(1024.0f)         :
                      static_cast <float> (cover.file_info.size),
                                           cover.file_info.size > 1024             ?
                                           cover.file_info.size > 1024*1024        ?
                                                                             "MiB" :
                                                                             "KiB" :
                                                                             "Bytes");
      }

      ImGui::TextUnformatted (szLabels);
      ImGui::SameLine        (posXvalues);
      ImGui::TextUnformatted (szLabelsData);

      if (_registry.bDeveloperMode)
        ImGui::TextUnformatted ("\n");
    }

    // Pure additional Developer Mode debug data
    if (_registry.bDeveloperMode)
    {
      char     szLabels      [512] = { };
      char     szLabelsData  [512] = { };

      sprintf (szLabels,     "Viewport Size:\n"
                             "Frame Size:\n");
      sprintf (szLabelsData, "%.0fx%.0f\n"
                             "%.0fx%.0f\n",
                              ImGui::GetMainViewport ( )->Size.x,
                              ImGui::GetMainViewport ( )->Size.y,
                              cover.avail_size.x,
                              cover.avail_size.y);

      ImGui::TextUnformatted (szLabels);
      ImGui::SameLine        (posXvalues);
      ImGui::TextUnformatted (szLabelsData);
    }

    // Basic Image Details
    {
      static const char szLabels [] = "Resolution:\n"
                                      "Zoom Level:\n"
                                      "Dynamic Range:\n";

      char     szLabelsData  [512] = { };

      sprintf (szLabelsData, "%.0fx%.0f\n"
                             "%3.0f %%\n"
                             "%s (%hs @ %d-bpc)\n",
                                      cover.width,
                                      cover.height,
                                      cover.zoom * 100,
                                     (cover.is_hdr) ? "HDR" : (cover.is_wcg) ? "SDR, wide gamut" : "SDR",
                                      cover.channels == 0 ? "Unknown" :
                                      cover.channels == 1 ? "R"       :
                                      cover.channels == 2 ? "RG"      :
                                      cover.channels == 3 ? "RGB"     :
                                      cover.channels == 4 ? "RGBA"    :
                                                            "Unknown",
                                      std::min (cover.bpc, 32));

      ImGui::TextUnformatted (szLabels);
      ImGui::SameLine        (posXvalues);
      ImGui::TextUnformatted (szLabelsData);

      if (cover.zoom != 1.0f)
      {
        ImGui::SameLine             ();
        ImGui::BeginGroup           ();
        ImGui::PopFont              ();
        ImGui::Spacing              ();
        ImGui::Spacing              ();
        if (ImGui::Button (ICON_FA_ROTATE_LEFT "###Zoom_Reset"))
          cover.zoom = 1.0f;
        ImGui::PushFont (fontConsolas, SKIF_ImGui_GetFontConsolasSize ( ));
        ImGui::EndGroup             ();
      }
    }

    // How the file describes its colors; everything it has, on hover
    {
      const std::string colorSpace = _DescribeColorSource (cover.color_source);

      ImGui::TextUnformatted ("Color Space:");
      ImGui::SameLine        (posXvalues);
      ImGui::TextUnformatted (colorSpace.c_str ());
      SKIF_ImGui_SetHoverTip (_DescribeColorSourceInDetail (cover.color_source));
    }

    // Animated images
    if (cover.anim != nullptr && ! cover.anim->frames.empty ())
    {
      const image_anim_s& anim  = *cover.anim;
      const size_t        frame = anim.current;

      char szAnimLabels [128] = { };
      char szAnimData   [256] = { };

      sprintf (szAnimLabels, "Frame:\n"
                             "Duration:\n");
      sprintf (szAnimData,   "%zu / %zu (%lu ms)%s\n"
                             "%.2f s%s\n",
                              frame + 1, anim.frames.size (),
                              anim.frameEnds [frame] - anim.getFrameStart (frame),
                              anim.paused ? ", paused" : "",
                              static_cast <float> (anim.duration) / 1000.0f,
                              (anim.resolution.x != cover.width) ? " (scaled down to fit)" : "");

      ImGui::TextUnformatted (szAnimLabels);
      ImGui::SameLine        (posXvalues);
      ImGui::TextUnformatted (szAnimData);
    }

    // HDR Light Levels
    if (cover.light_info.isHDR)
    {
      ImGui::TextUnformatted ("\n");

      static const char szLightLabels [] = "MaxCLL (scRGB): \n"
                                           "Max Luminance: \n"
                                           "Avg Luminance: \n"
                                           "Min Luminance: ";
      char     szLightUnits  [512] = { };
      char     szLightLevels [512] = { };

      sprintf (szLightUnits, (const char*)
                           u8"(%c)\n"
                           u8"cd / m\u00b2\n" // Unicode: Superscript Two
                           u8"cd / m\u00b2\n"
                           u8"cd / m\u00b2", cover.light_info.max_cll_name);
      sprintf (szLightLevels, "%.3f \n"
                              "%.3f \n"
                              "%.3f \n"
                              "%.3f ",  cover.light_info.max_cll,
                                        cover.light_info.max_nits,
                                        cover.light_info.avg_nits,
                                        cover.light_info.min_nits);

      auto orig_pos =
        ImGui::GetCursorPos ();

      //ImGui::SetCursorPos    (ImVec2 (0.0f, 0.0f));
      ImGui::TextUnformatted (szLightLabels);
      ImGui::SameLine        (posXvalues);
      ImGui::TextUnformatted (szLightLevels);
      ImGui::SameLine        ();
      ImGui::TextUnformatted (szLightUnits);

      auto light_pos =
        ImGui::GetCursorPos ();

      ImGui::SetCursorPos    (orig_pos);

      const float fPercent709       = cover.colorimetry.pixel_counts.getPercentRec709    ();
      const float fPercentP3        = cover.colorimetry.pixel_counts.getPercentDCIP3     ();
      const float fPercent2020      = cover.colorimetry.pixel_counts.getPercentRec2020   ();
      const float fPercentAP1       = cover.colorimetry.pixel_counts.getPercentAP1       ();
      const float fPercentAP0       = cover.colorimetry.pixel_counts.getPercentAP0       ();
      const float fPercentUndefined = cover.colorimetry.pixel_counts.getPercentUndefined ();

      ImGui::SetCursorPos    (ImVec2 (orig_pos.x, light_pos.y));
      ImGui::TextUnformatted ("\n");

      ImGui::BeginGroup ();
      if (SKIV_HDR_VisualizationId == SKIV_HDR_VISUALIZTION_GAMUT)
      {
        if (fPercent709       > 0.0f)  ImGui::TextColored (ImVec4 (SKIV_HDR_GamutHue_Rec709 [0],
                                                                   SKIV_HDR_GamutHue_Rec709 [1],
                                                                   SKIV_HDR_GamutHue_Rec709 [2], 1.0f),
                                                                   "Rec. 709:\t\t ");
        if (fPercentP3        > 0.001f) ImGui::TextColored (ImVec4 (SKIV_HDR_GamutHue_DciP3 [0],
                                                                    SKIV_HDR_GamutHue_DciP3 [1],
                                                                    SKIV_HDR_GamutHue_DciP3 [2], 1.0f),
                                                                    "DCI-P3:\t\t ");
        if (fPercent2020      > 0.001f) ImGui::TextColored (ImVec4 (SKIV_HDR_GamutHue_Rec2020 [0],
                                                                    SKIV_HDR_GamutHue_Rec2020 [1],
                                                                    SKIV_HDR_GamutHue_Rec2020 [2], 1.0f),
                                                                    "Rec. 2020:\t\t ");
        if (fPercentAP1       > 0.001f) ImGui::TextColored (ImVec4 (SKIV_HDR_GamutHue_Ap1 [0],
                                                                    SKIV_HDR_GamutHue_Ap1 [1],
                                                                    SKIV_HDR_GamutHue_Ap1 [2], 1.0f),
                                                                    "ACES AP1:\t\t ");
        if (fPercentAP0       > 0.001f) ImGui::TextColored (ImVec4 (SKIV_HDR_GamutHue_Ap0 [0],
                                                                    SKIV_HDR_GamutHue_Ap0 [1],
                                                                    SKIV_HDR_GamutHue_Ap0 [2], 1.0f),
                                                                    "ACES AP0:\t\t ");
        if (fPercentUndefined > 0.001f) ImGui::TextColored (ImVec4 (SKIV_HDR_GamutHue_Undefined [0],
                                                                    SKIV_HDR_GamutHue_Undefined [1],
                                                                    SKIV_HDR_GamutHue_Undefined [2], 1.0f),
                                                                    "Undefined:\t\t ");
      }
      else
      {
        if (fPercent709       > 0.0f)   ImGui::TextUnformatted ("Rec. 709:\t\t " );
        if (fPercentP3        > 0.001f) ImGui::TextUnformatted ("DCI-P3:\t\t "   );
        if (fPercent2020      > 0.001f) ImGui::TextUnformatted ("Rec. 2020:\t\t ");
        if (fPercentAP1       > 0.001f) ImGui::TextUnformatted ("ACES AP1:\t\t " );
        if (fPercentAP0       > 0.001f) ImGui::TextUnformatted ("ACES AP0:\t\t " );
        if (fPercentUndefined > 0.001f) ImGui::TextUnformatted ("Undefined:\t\t ");
      }
      ImGui::EndGroup   ();
      ImGui::SameLine   (posXvalues);
      ImGui::BeginGroup ();
      if (fPercent709       > 0.0f)   ImGui::Text ("%8.4f %%", fPercent709);
      if (fPercentP3        > 0.001f) ImGui::Text ("%8.4f %%", fPercentP3);
      if (fPercent2020      > 0.001f) ImGui::Text ("%8.4f %%", fPercent2020);
      if (fPercentAP1       > 0.001f) ImGui::Text ("%8.4f %%", fPercentAP1);
      if (fPercentAP0       > 0.001f) ImGui::Text ("%8.4f %%", fPercentAP0);
      if (fPercentUndefined > 0.001f) ImGui::Text ("%8.4f %%", fPercentUndefined);
      ImGui::EndGroup   ();

      ImGui::Spacing ();

      SKIF_ImGui_OptImage  ( cover.pGamutCoverageSRV,
                             ImVec2 (256.0f, 256.0f),
                             cover.uv0, // Top Left coordinates
                             cover.uv1, // Bottom Right coordinates
                             ImVec4 (1.0f, 1.0f, 1.0f, 1.0f)
      );

      ImGui::PopFont ();

      ImGui::SameLine   ();
      ImGui::BeginGroup ();

#if 1
      ImGui::TextUnformatted ("Output Format");

      extern bool RecreateSwapChains;
      static int* ptrSDR = nullptr;

      if ((_registry.iHDRMode > 0 && SKIF_Util_IsHDRActive (NULL)))
      {
        ptrSDR = &_registry.iHDRMode;
      }
      else
        ptrSDR = &_registry.iSDRMode;

      if (ImGui::RadioButton   (" 8 bpc SDR", ptrSDR, 0))
      {
        _registry.iHDRMode = 0;
        _registry.iSDRMode = 0;
        _registry.regKVSDRMode.putData (_registry.iSDRMode);
        _registry.regKVHDRMode.putData (_registry.iHDRMode);

        RecreateSwapChains = true;
      }
      // It seems that Windows 10 1709+ (Build 16299) is required to
      // support 10 bpc (DXGI_FORMAT_R10G10B10A2_UNORM) for flip model
      if (SKIF_Util_IsWindows10v1709OrGreater ( ))
      {
        if (ImGui::RadioButton ("10 bpc SDR", ptrSDR, 1))
        {
          _registry.iHDRMode = 0;
          _registry.iSDRMode = 1;
          _registry.regKVSDRMode.putData (_registry.iSDRMode);
          _registry.regKVHDRMode.putData (_registry.iHDRMode);

          RecreateSwapChains = true;
        }
      }
      if (SKIF_Util_IsHDRActive (NULL))
      {
        if (ImGui::RadioButton ("16 bpc HDR", &_registry.iHDRMode, 2))
        {
          _registry.iSDRMode = 0;
          _registry.regKVSDRMode.putData (_registry.iSDRMode);
          _registry.regKVHDRMode.putData (_registry.iHDRMode);

          RecreateSwapChains = true;
        }
      }

      ImGui::Spacing (); ImGui::Spacing (); ImGui::Spacing (); ImGui::Spacing ();
      //ImGui::TextUnformatted ("");

      if (ImGui::Button (ICON_FA_FLOPPY_DISK "  Save As..."))
        SaveFileDialog  = PopupState_Open;
      if (ImGui::Button (ICON_FA_FILE_EXPORT " Export to SDR"))
        ExportSDRDialog = PopupState_Open;

      ImGui::Spacing (); ImGui::Spacing (); ImGui::Spacing (); ImGui::Spacing ();
      //ImGui::TextUnformatted ("");

      if (ImGui::Button (ICON_FA_CLIPBOARD "  Copy to Clipboard"))
      {
        wantCopyToClipboard = true;
      }

      //static int clipboard_type = 0;
      //ImGui::RadioButton ("HDR (.png)", &clipboard_type, 0); ImGui::SameLine ();
      //ImGui::RadioButton ("SDR",        &clipboard_type, 1);
#endif
      ImGui::EndGroup ();

      ImGui::Spacing (); ImGui::Spacing ();

      ImGui::SliderFloat ("Brightness", &SKIV_HDR_BrightnessScale, 1.0f, 2000.0f, "%.3f %%", ImGuiSliderFlags_Logarithmic);

      float slider_width =
        ImGui::CalcItemWidth ();

      if (SKIV_HDR_BrightnessScale != 100.0f)
      {
        ImGui::SameLine ();
        if (ImGui::Button (ICON_FA_ROTATE_LEFT "###Brightness_Reset")) SKIV_HDR_BrightnessScale = 100.0f;
      }

      if (_registry._RendererHDREnabled && _registry.iHDRMode == 2)
      {
        float fCursorX1 =
          ImGui::GetCursorPosX ();

        static bool  bLockCalibration     = true;
        static float fCalibrationOverride = 0.0f;

        if (ImGui::Button ( bLockCalibration ? ICON_FA_LOCK     "###LockCalibration"
                                             : ICON_FA_UNLOCK "###UnlockCalibration" ))
        {
          bLockCalibration =
             (! bLockCalibration);
          if (! bLockCalibration)
          {
            if ( fCalibrationOverride >= 0.0f )
                 fCalibrationOverride = std::min (-SKIV_HDR_DisplayMaxLuminance,
                                                   SKIV_HDR_DisplayMaxLuminance);
          }

          else
          {
            fCalibrationOverride =
              std::max (-fCalibrationOverride,
                         fCalibrationOverride);
          }
        }

        if (bLockCalibration) ImGui::BeginDisabled ();

        ImGui::SameLine ();

        float fCursorX2 =
          ImGui::GetCursorPosX ();

        if (fCalibrationOverride != 0.0f)
          SKIV_HDR_DisplayMaxLuminance = bLockCalibration ? abs (fCalibrationOverride)
                                                          :      fCalibrationOverride;

        ImGui::SetNextItemWidth (
          (slider_width - (fCursorX2 - fCursorX1))
        );

        static constexpr float _fMinStdHdrLuminance =  300.0f;
        static constexpr float _fMaxStdHdrLuminance = 1500.0f;

        if (fCalibrationOverride != 0.0f)
        {
          float override_slider =
            abs (fCalibrationOverride);

          if (ImGui::SliderFloat ( "Display Luminance", &override_slider,
                              _fMinStdHdrLuminance, _fMaxStdHdrLuminance,
                                   (const char *)u8"%.1f cd / m\u00b2", ImGuiSliderFlags_Logarithmic ))
          {
            SKIV_HDR_DisplayMaxLuminance = -override_slider;
            fCalibrationOverride         = -override_slider;
          }

          if (ImGui::IsItemHovered ())
          {
            ImGui::BeginTooltip    ( );
            ImGui::TextUnformatted ("Ctrl-Click to Manually Input Precise Values");
            ImGui::Separator       ( );
            ImGui::BulletText      ("Accurate maximum luminance is necessary to display content "
                                    "brighter than your display supports.");
            ImGui::Separator       ( );
            ImGui::TextUnformatted (ICON_FA_LOCK " the slider to dismiss the test pattern.");
            ImGui::EndTooltip      ( );
          }
        }

        else if (ImGui::SliderFloat ( "Display Luminance", &SKIV_HDR_DisplayMaxLuminance,
                                              _fMinStdHdrLuminance, _fMaxStdHdrLuminance,
                                                   (const char *)u8"%.1f cd / m\u00b2", ImGuiSliderFlags_Logarithmic ))
        {
          fCalibrationOverride =
            -SKIV_HDR_DisplayMaxLuminance;
        }
        if (ImGui::IsItemHovered (ImGuiHoveredFlags_AllowWhenDisabled) && bLockCalibration)
        {
          ImGui::BeginTooltip    (  );
          ImGui::TextUnformatted ("Validate or Override EDID/ICC Profile Display Maximum Luminance");
          ImGui::Separator       (  );
          ImGui::Spacing         (  );
          ImGui::PushStyleColor  (ImGuiCol_Text, ImVec4 (.55f, .55f, .55f, 1.f));
          ImGui::BulletText      ("Many TVs do not supply actual luminance capabilities to Windows !!");
          ImGui::BulletText      ("Do not trust SKIV's default values unless you have run Windows 11 HDR Calibration.");
          ImGui::PopStyleColor   (  );
          ImGui::Spacing         (  );
          ImGui::Spacing         (  );
          ImGui::TextUnformatted ("To validate display luminance clipping, " ICON_FA_UNLOCK " the slider and adjust"
                                  " until the white checkerboard pattern appears solid white.");
          ImGui::Spacing         (  );
          ImGui::PushStyleColor  (ImGuiCol_Text, ImVec4 (.55f, .55f, .55f, 1.f));
          ImGui::BulletText      ("Find the -smallest- value that causes the test pattern to disappear (clip).");
          ImGui::PopStyleColor   (  );
          ImGui::Spacing         (  );
          ImGui::Spacing         (  );
          ImGui::TextUnformatted ("If the default values fail to clip, run Windows HDR Calibration and/or disable "
                                  "driver color management (i.e. Reference Mode).");
          ImGui::Spacing         (  );
          ImGui::Spacing         (  );
          ImGui::Separator       (  );
          ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), "%hs", ICON_FA_CIRCLE_INFO); ImGui::SameLine ();
          ImGui::TextUnformatted ("This test measures signal processing / driver color management bugs; it cannot measure "
                                  "ABL or physical light output.");
          ImGui::EndTooltip      (  );
        }
        if (bLockCalibration) ImGui::EndDisabled ();

        if (fCalibrationOverride != 0.0f)
        {
          ImGui::SameLine ();
          if (ImGui::Button (ICON_FA_ROTATE_LEFT "###Calibration_Reset"))
                                                    fCalibrationOverride = 0.0f;
        }

        // Do not show content range warnings while the override value is negative,
        //   user is calibrating their screen...
        if (bLockCalibration)
        {
          bool changed_tonemap = false;

          if ( (SKIV_HDR_BrightnessScale / 100.0f) * SKIV_HDR_MaxLuminance >
                                                     SKIV_HDR_DisplayMaxLuminance )
          {
            ImGui::Spacing (); ImGui::Spacing ();
            ImGui::TextColored (ImColor (0xff0099ff), ICON_FA_TRIANGLE_EXCLAMATION);
            ImGui::SameLine    ();
            ImGui::TextUnformatted ("Content Exceeds Display Capabilities");

            changed_tonemap |=
            ImGui::RadioButton ("Do Nothing",      &_registry.iHDRToneMapType, SKIV_TONEMAP_TYPE_NONE);
            if (ImGui::IsItemHovered ())
            {
              ImGui::BeginTooltip    ( );
              ImGui::TextUnformatted ("If Content Exceeds Display's Maximum Luminance:\t");
              ImGui::SameLine        ( );
              ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), "Nasal Demons...?");
              ImGui::Separator       ( );
              ImGui::PushStyleColor  (ImGuiCol_Text, ImVec4 (.65f, .65f, .65f, 1.f));
              ImGui::BulletText      ("You are going to need a pixel exorcist.");
              ImGui::PopStyleColor   ( );
              ImGui::EndTooltip      ( );
            }
            ImGui::SameLine    ();

            changed_tonemap |=
            ImGui::RadioButton ("Clip to Display", &_registry.iHDRToneMapType, SKIV_TONEMAP_TYPE_CLIP);
            if (ImGui::IsItemHovered ())
            {
              ImGui::BeginTooltip    ( );
              ImGui::TextUnformatted ("If Content Exceeds Display's Maximum Luminance:\t");
              ImGui::SameLine        ( );
              ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Yellow), "Clip Luminance at Display's Limit");
              ImGui::Separator       ( );
              ImGui::PushStyleColor  (ImGuiCol_Text, ImVec4 (.65f, .65f, .65f, 1.f));
              ImGui::BulletText      ("Produces the brightest highlights possible, even if it means loss of image detail.");
              ImGui::BulletText      ("Some HDR scenes are intentionally too bright, and clipping is intended behavior.");
              ImGui::PopStyleColor   ( );
              ImGui::EndTooltip      ( );
            }
            ImGui::SameLine          ( );

            changed_tonemap |=
            ImGui::RadioButton ("Map to Display",  &_registry.iHDRToneMapType, SKIV_TONEMAP_TYPE_MAP_CLL_TO_DISPLAY);
            if (ImGui::IsItemHovered ())
            {
              ImGui::BeginTooltip    ( );
              ImGui::TextUnformatted ("If Content Exceeds Display's Maximum Luminance:\t");
              ImGui::SameLine        ( );
              ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success), "Tone-map for Maximum Visibility");
              ImGui::Separator       ( );
              ImGui::PushStyleColor  (ImGuiCol_Text, ImVec4 (.65f, .65f, .65f, 1.f));
              ImGui::BulletText      ("Causes some loss in white luminance on highlights.");
              ImGui::BulletText      ("May expose details you were not intended to see (i.e. calibration test patterns).");
              ImGui::PopStyleColor   ( );
              ImGui::EndTooltip      ( );
            }

            if (changed_tonemap)
               _registry.regKVHDRToneMapType.putData (_registry.iHDRToneMapType);

            if (_registry.iHDRToneMapType == SKIV_TONEMAP_TYPE_MAP_CLL_TO_DISPLAY)
            {
              if (ImGui::Checkbox ("Use 99th Percentile Luminance as MaxCLL", &_registry.b99thPercentileMaxCLL))
                _registry.regKV99thPercentileMaxCLL.putData                   (_registry.b99thPercentileMaxCLL);

              if (ImGui::IsItemHovered ())
                ImGui::SetTooltip ("Increase brightness on scenes with very bright localized peak highlights");
            }
          }
        }
      }

      ImGui::PushFont (fontConsolas, SKIF_ImGui_GetFontConsolasSize ( ));

      if (SKIV_HDR_VisualizationId == SKIV_HDR_VISUALIZTION_SDR)
      {
        ImGui::TextUnformatted ("");

        ImGui::TextUnformatted ("SDR Grayscale Settings");
        ImGui::TreePush        ("");

        bool luminance = (SKIV_HDR_VisualizationFlagsSDR & SKIV_VIZ_FLAG_SDR_CONSIDER_LUMINANCE);
        if (ImGui::Checkbox ("Test HDR Luminance", &luminance))
        {
          if (luminance) SKIV_HDR_VisualizationFlagsSDR |=  SKIV_VIZ_FLAG_SDR_CONSIDER_LUMINANCE;
          else           SKIV_HDR_VisualizationFlagsSDR &= ~SKIV_VIZ_FLAG_SDR_CONSIDER_LUMINANCE;
        }

        bool gamut = (SKIV_HDR_VisualizationFlagsSDR & SKIV_VIZ_FLAG_SDR_CONSIDER_GAMUT);
        if (ImGui::Checkbox ("Test HDR Gamut", &gamut))
        {
          if (gamut) SKIV_HDR_VisualizationFlagsSDR |=  SKIV_VIZ_FLAG_SDR_CONSIDER_GAMUT;
          else       SKIV_HDR_VisualizationFlagsSDR &= ~SKIV_VIZ_FLAG_SDR_CONSIDER_GAMUT;
        }

        bool overbright = (SKIV_HDR_VisualizationFlagsSDR & SKIV_VIZ_FLAG_SDR_CONSIDER_OVERBRIGHT);
        if (ImGui::Checkbox ("Test HDR Overbright", &overbright))
        {
          if (overbright) SKIV_HDR_VisualizationFlagsSDR |=  SKIV_VIZ_FLAG_SDR_CONSIDER_OVERBRIGHT;
          else            SKIV_HDR_VisualizationFlagsSDR &= ~SKIV_VIZ_FLAG_SDR_CONSIDER_OVERBRIGHT;
        }

        ImGui::TreePop ();
      }

      ImGui::SetCursorPos (orig_pos);
    }

    // Wide gamut SDR images: their gamut coverage, as HDR images show it above (without their light levels). The rows
    //   take the colors of the Gamut Coverage visualization while it is shown.
    if (cover.is_wcg && ! cover.light_info.isHDR)
    {
      ImGui::TextUnformatted ("\n");

      struct gamut_row_s { const char* label; float percent; const float* hue; };

      const auto&       counts = cover.colorimetry.pixel_counts;
      const gamut_row_s rows [] = {
        { "Rec. 709:",  counts.getPercentRec709    (), SKIV_HDR_GamutHue_Rec709    },
        { "DCI-P3:",    counts.getPercentDCIP3     (), SKIV_HDR_GamutHue_DciP3     },
        { "Rec. 2020:", counts.getPercentRec2020   (), SKIV_HDR_GamutHue_Rec2020   },
        { "ACES AP1:",  counts.getPercentAP1       (), SKIV_HDR_GamutHue_Ap1       },
        { "ACES AP0:",  counts.getPercentAP0       (), SKIV_HDR_GamutHue_Ap0       },
        { "Undefined:", counts.getPercentUndefined (), SKIV_HDR_GamutHue_Undefined }
      };

      for (const auto& row : rows)
      {
        if (row.percent <= 0.001f)
          continue;

        if (SKIV_HDR_VisualizationId == SKIV_HDR_VISUALIZTION_GAMUT)
          ImGui::TextColored   (ImVec4 (row.hue [0], row.hue [1], row.hue [2], 1.0f), "%s", row.label);
        else
          ImGui::TextUnformatted (row.label);

        ImGui::SameLine (posXvalues);
        ImGui::Text     ("%8.4f %%", row.percent);
      }

      ImGui::Spacing ();

      SKIF_ImGui_OptImage ( cover.pGamutCoverageSRV,
                            ImVec2 (256.0f, 256.0f),
                            cover.uv0, // Top Left coordinates
                            cover.uv1, // Bottom Right coordinates
                            ImVec4 (1.0f, 1.0f, 1.0f, 1.0f)
      );

      if (SKIV_HDR_VisualizationId != SKIV_HDR_VISUALIZTION_GAMUT)
        SKIF_ImGui_SetHoverTip ("Charted while the Gamut Coverage visualization is shown (context menu: Visualization).");
    }

    ImGui::PopFont      ( ); // fontConsolas
    ImGui::EndChild     ( ); // ###ImageDetails
    ImGui::SetCursorPos (parent_pos);
  }

#pragma endregion

#pragma region ContextMenu

  auto _IsRightClicked = [&](void) -> bool
  {
    if (ImGui::IsMouseClicked (ImGuiMouseButton_Right))
    {
      return true;
    }

    // Activate button held for >= .4 seconds -> right-click
    if (ImGui::GetKeyData (ImGuiKey_GamepadFaceDown)->DownDuration > 0.4f &&
        ImGui::GetKeyData (ImGuiKey_GamepadFaceDown)->DownDuration < 5.0f)
    {
      ImGui::GetKeyData (ImGuiKey_GamepadFaceDown)->DownDuration     = 5.0f;
      ImGui::GetKeyData (ImGuiKey_GamepadFaceDown)->DownDurationPrev = 0.0f;

      ImGui::ClearActiveID ( );

      return true;
    }

    // Start button = Menu
    if (ImGui::IsKeyPressed (ImGuiKey_GamepadStart))
    {
      ImGui::GetKeyData (ImGuiKey_GamepadStart)->DownDuration     =  0.01f;
      ImGui::GetKeyData (ImGuiKey_GamepadStart)->DownDurationPrev =  0.00f;

      ImGui::ClearActiveID ( );

      return true;
    }

    return false;
  };

  // Act on all right clicks, because why not? :D
  if (! SKIF_ImGui_IsAnyPopupOpen ( ) && _IsRightClicked ())
    ContextMenu = PopupState_Open;

  // Open the Empty Space Menu
  if (ContextMenu == PopupState_Open)
    ImGui::OpenPopup    ("ContextMenu");


  if (ConfigEncoders == PopupState_Open)
  {
    ImGui::OpenPopup ("ConfigEncoders");
    ConfigEncoders = PopupState_Opened;
  }

  if (ConfigEncoders == PopupState_Opened)
  {
    if (ImGui::BeginPopup ("ConfigEncoders", ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::BeginTabBar ("EncoderTabs");
      static int selection = 0;

      if (ImGui::BeginTabItem ("AVIF", nullptr, ImGuiTabItemFlags_NoTooltip))
      {
        selection = 0;

        if (ImGui::SliderInt ("Compression Speed",    &_registry.avif.speed,  AVIF_SPEED_SLOWEST, AVIF_SPEED_FASTEST))
          _registry.regKVAVIFSpeed.putData (_registry.avif.speed);

        if (ImGui::SliderInt ("Compression Quality", &_registry.avif.quality, 80, 100))
          _registry.regKVAVIFQuality.putData         (_registry.avif.quality);

        int avif_bit_select =
          _registry.avif.hdr_bitdepth == 8  ? 0 :
          _registry.avif.hdr_bitdepth == 10 ? 1 :
          _registry.avif.hdr_bitdepth == 12 ? 2 : 2;

        bool changed_depth =
        ImGui::Combo     ("Compression Bit Depth", &avif_bit_select, " 8-bpc\0 10-bpc\0 12-bpc\0\0");

        _registry.avif.hdr_bitdepth = avif_bit_select == 0 ?  8 :
                                      avif_bit_select == 1 ? 10 :
                                      avif_bit_select == 2 ? 12 : 12;

        if (changed_depth)
          _registry.regKVAVIFHDRBitDepth.putData (_registry.avif.hdr_bitdepth);

        ImGui::EndTabItem      ();
      }
      if (ImGui::BeginTabItem ("JPEG XL", nullptr, ImGuiTabItemFlags_NoTooltip))
      {
        selection = 1;

        if (ImGui::SliderInt ("Compression Speed", &_registry.jxl.speed, AVIF_SPEED_SLOWEST, AVIF_SPEED_FASTEST))
          _registry.regKVJXLSpeed.putData          (_registry.jxl.speed);

        if (ImGui::SliderInt ("Compression Quality", &_registry.jxl.quality, 80, 100))
          _registry.regKVJXLQuality.putData          (_registry.jxl.quality);
        ImGui::EndTabItem      ();
      }
      ///if (ImGui::BeginTabItem ("JPEG XR", nullptr, ImGuiTabItemFlags_NoTooltip))
      ///{
      ///  selection = 2;
      ///  //ImGui::TextUnformatted ("XR");
      ///  static int                                jxr_compression_quality = 100;
      ///  ImGui::SliderInt ("Compression Quality", &jxr_compression_quality, 80, 100);
      ///  ImGui::EndTabItem      ();
      ///}
      if (ImGui::BeginTabItem ("PNG", nullptr, ImGuiTabItemFlags_NoTooltip))
      {
        selection = 3;
        if (ImGui::SliderInt ("HDR Bit Depth", &_registry.png.hdr_bitdepth, 10, 16))
          _registry.regKVPNGHDRBitDepth.putData(_registry.png.hdr_bitdepth);
        ImGui::EndTabItem      ();
      }
      //if (ImGui::BeginTabItem ("Ultra HDR", nullptr, ImGuiTabItemFlags_NoTooltip))
      //{
      //}
      ImGui::EndTabBar   ();
      if (ImGui::Button ("Okay"))
      {
        ImGui::CloseCurrentPopup ();
      }
      ImGui::EndPopup     ();
    }

    else
    {
      ConfigEncoders = PopupState_Closed;
    }
  }

  if (ImGui::BeginPopup   ("ContextMenu", ImGuiWindowFlags_NoMove))
  {
    ContextMenu = PopupState_Opened;
    constexpr char spaces[] = { "\u0020\u0020\u0020\u0020" };

    ImGui::PushStyleColor (ImGuiCol_NavCursor, ImVec4(0,0,0,0));

    if (SKIF_ImGui_MenuItemEx2 ("Open", ICON_FA_EYE, ImGui::GetStyleColorVec4(ImGuiCol_Text), "Ctrl+A"))
      OpenFileDialog = PopupState_Open;

    if (tryingToDownImage)
    {
      ImGui::PushStyleColor  (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
      SKIF_ImGui_MenuItemEx2 ("Downloading...", ICON_FA_SPINNER, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::PopStyleColor   ( );
    }

    else if (tryingToLoadImage)
    {
      ImGui::PushStyleColor  (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
      SKIF_ImGui_MenuItemEx2 ("Loading...", ICON_FA_SPINNER, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::PopStyleColor   ( );
    }

    else if (cover.pRawTexSRV.p != nullptr)
    {
      if (SKIF_ImGui_MenuItemEx2 ("Save As...", ICON_FA_FLOPPY_DISK,    ImGui::GetStyleColorVec4(ImGuiCol_Text),      "Ctrl+S"))
        SaveFileDialog = PopupState_Open;
      if (cover.is_hdr &&
          SKIF_ImGui_MenuItemEx2 ("Export to SDR", ICON_FA_FILE_EXPORT, ImGui::GetStyleColorVec4(ImGuiCol_Text),      "Ctrl+X"))
        ExportSDRDialog = PopupState_Open;

      ImGui::Separator ( );

      if (SKIF_ImGui_MenuItemEx2 ("Copy",         ICON_FA_CLIPBOARD,    ImGui::GetStyleColorVec4(ImGuiCol_Text),      "Ctrl+C"))
        wantCopyToClipboard = true;

      // Animated images
      if (cover.anim != nullptr)
      {
        const DWORD now = SKIF_Util_timeGetTime ( );

        if (cover.anim->paused)
        {
          if (SKIF_ImGui_MenuItemEx2 ("Play",     ICON_FA_PLAY,         ImGui::GetStyleColorVec4(ImGuiCol_Text),      "Space"))
            cover.anim->play  (now);
        }

        else if (SKIF_ImGui_MenuItemEx2 ("Pause", ICON_FA_PAUSE,        ImGui::GetStyleColorVec4(ImGuiCol_Text),      "Space"))
          cover.anim->pause (now);

        if (SKIF_ImGui_MenuItemEx2 ("Next frame", 0,                    ImGui::GetStyleColorVec4(ImGuiCol_Text),      "."))
          cover.anim->step (now,  1);
        if (SKIF_ImGui_MenuItemEx2 ("Previous frame", 0,                ImGui::GetStyleColorVec4(ImGuiCol_Text),      ","))
          cover.anim->step (now, -1);
      }
      if (SKIF_ImGui_MenuItemEx2 ("Close", 0,                           ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), "Ctrl+W"))
        _SwapOutCover ();
      if (_current_folder.fileList.getActiveFile() != nullptr && ! _current_folder.fileList.getActiveFile()->path.empty() &&
          SKIF_ImGui_MenuItemEx2 ("Delete", ICON_FA_TRASH_CAN,          ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure), "Delete"))
        _DeleteImage ();

      // Image scaling

      ImGui::Separator ( );

      ImGui::PushID ("#ImageScaling");

      if (SKIF_ImGui_BeginMenuEx2 ("Scaling", 0)) // ICON_FA_PANORAMA
      {
        auto _CreateMenuItem = [&](ImageScaling image_scaling, const char* label, const char* shortcut) {
          bool bEnabled = (cover.scaling == image_scaling);

          if (bEnabled)
            ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));

          if (ImGui::MenuItem (label, shortcut, (cover.scaling == image_scaling)))
          {
            cover.scaling = (cover.scaling != image_scaling) ? image_scaling : ImageScaling_Auto;
            cover.zoom    = 1.0f;
          }

          if (bEnabled)
            ImGui::PopStyleColor  ( );
        };

        _CreateMenuItem (ImageScaling_None,    "View actual size", "Ctrl+1");
        _CreateMenuItem (ImageScaling_Fit,     "Zoom to fit",      "Ctrl+2");
        _CreateMenuItem (ImageScaling_Fill,    "Fill window",      "Ctrl+3");

#if _DEBUG
        _CreateMenuItem (ImageScaling_Stretch, "Stretch",       "Ctrl+4");
#endif

        ImGui::EndMenu ( );
      }

      ImGui::PopID ( ); // #ImageScaling

      // HDR images, and wide gamut SDR ones (their gamut coverage only)
      if (cover.is_hdr || cover.is_wcg)
      {
        ImGui::PushID ("#HDRVisualization");

        if (SKIF_ImGui_BeginMenuEx2 ("Visualization", ICON_FA_EYE))
        {
          static bool bNone    = true;
          static bool bHeatmap = false;
          static bool bGamut   = false;
          static bool bSDR     = false;

          auto _ResetSelection = [&](bool& new_selection) {
            bNone    = false;
            bHeatmap = false;
            bGamut   = false;
            bSDR     = false;

            new_selection = true;
          };

          if (SKIF_ImGui_MenuItemEx2 ("None", ICON_FA_BAN, ImGui::GetStyleColorVec4 (ImGuiCol_Text), spaces, &bNone))
          {
            _ResetSelection (bNone);
            SKIV_HDR_VisualizationId = SKIV_HDR_VISUALIZTION_NONE;
          }

          if (cover.is_hdr &&
              SKIF_ImGui_MenuItemEx2 ("Luminance Heatmap", ICON_FA_CIRCLE_RADIATION, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Yellow), spaces, &bHeatmap))
          {
            _ResetSelection (bHeatmap);
            SKIV_HDR_VisualizationId = SKIV_HDR_VISUALIZTION_HEATMAP;
          }

          if (SKIF_ImGui_MenuItemEx2 ("Gamut Coverage", ICON_FA_PALETTE, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Icon), spaces, &bGamut))
          {
            _ResetSelection (bGamut);
            SKIV_HDR_VisualizationId = SKIV_HDR_VISUALIZTION_GAMUT;
          }

          if (cover.is_hdr &&
              SKIF_ImGui_MenuItemEx2 ("SDR Grayscale", ICON_FA_CIRCLE_HALF_STROKE, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Icon), spaces, &bSDR))
          {
            _ResetSelection (bSDR);
            SKIV_HDR_VisualizationId = SKIV_HDR_VISUALIZTION_SDR;
          }

          ImGui::EndMenu ( );
        }

        ImGui::PopID ( ); // #HDRVisualization
      }

      if (SKIF_ImGui_MenuItemEx2 ("Details", 0, ImGui::GetStyleColorVec4 (ImGuiCol_Text), "Ctrl+D", &_registry.bImageDetails)) // ICON_FA_BARCODE
        _registry.regKVImageDetails.putData (_registry.bImageDetails);

      if (SKIF_ImGui_MenuItemEx2 ("Encoder Setup", 0,                   ImGui::GetStyleColorVec4(ImGuiCol_Text),      "Ctrl+B")) // ICON_FA_GEARS
        ConfigEncoders = PopupState_Open;

      ImGui::Separator       ( );

      if (! cover.file_info.path.empty())
      {
        if (SKIF_ImGui_MenuItemEx2 ("Browse Folder", ICON_FA_FOLDER_OPEN, ImColor(255, 207, 72), "Ctrl+E"))
          SKIF_Util_FileExplorer_SelectFile (cover.file_info.path.c_str());

        if (SKIF_ImGui_MenuItemEx2 ("Context Menu", ICON_FA_WINDOWS,      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info)))
          SKIF_Util_FileExplorer_ContextMenuFile (cover.file_info.path.c_str(), SKIF_Notify_hWnd);
      }
    }

    ImGui::Separator       ( );

    if (SKIF_ImGui_MenuItemEx2 ("Settings", 0)) // ICON_FA_LIST_CHECK
      SKIF_Tab_ChangeTo = UITab_Settings;

    ImGui::Separator ( );

    if (SKIF_ImGui_MenuItemEx2 ("Fullscreen", SKIF_ImGui_IsFullscreen (SKIF_ImGui_hWnd) ? ICON_FA_DOWN_LEFT_AND_UP_RIGHT_TO_CENTER : ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, ImGui::GetStyleColorVec4 (ImGuiCol_Text), "Ctrl+F"))
    {
      SKIF_ImGui_SetFullscreen (SKIF_ImGui_hWnd, ! SKIF_ImGui_IsFullscreen (SKIF_ImGui_hWnd));
    }

    //if (SKIF_ImGui_MenuItemEx2 ("Snipping Mode", ICON_FA_SCISSORS, ImGui::GetStyleColorVec4 (ImGuiCol_Text), ""))
    //{
    //  _registry._SnippingMode = ! _registry._SnippingMode;
    //}

    ImGui::Separator ( );

    if (_registry.bCloseToTray)
    {
      if (SKIF_ImGui_MenuItemEx2 ("Close app", 0, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), "Esc"))
        PostMessage (SKIF_Notify_hWnd, WM_SKIF_MINIMIZE, 0x0, 0x0);
    }

    else
    {
      if (SKIF_ImGui_MenuItemEx2 ("Minimize", 0, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), "Ctrl+N"))
        PostMessage (SKIF_Notify_hWnd, WM_SKIF_MINIMIZE, 0x0, 0x0);
    }

    if (SKIF_ImGui_MenuItemEx2 ("Exit", 0, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), "Ctrl+Q"))
    {
      extern bool bKeepWindowAlive;
      bKeepWindowAlive = false;
    }


    ImGui::PopStyleColor  ( );
    ImGui::EndPopup       ( );
  }

  else
    ContextMenu = PopupState_Closed;

#pragma endregion


#pragma region SKIF_ImageWorker

  if (loadImage)
  {
    loadImage = false;

    // Reset variables used to track whether we're still loading a game cover, or if we're missing one
    imageLoading.store (true);
    tryingToLoadImage = true;
    queuePosGameCover = textureLoadQueueLength.load() + 1;

    // Update the title of the main app window to indicate we're loading...
    if (SKIF_ImGui_hWnd != NULL)
      ::SetWindowText (SKIF_ImGui_hWnd, L"Loading... - " SKIV_WINDOW_TITLE_SHORT_W);

    struct thread_s {
      image_s image = { };
    };

    thread_s* data = new thread_s;

    data->image.file_info.path      = new_path;
    data->image.file_info.path_utf8 = SK_WideCharToUTF8 (new_path);
    data->image.file_info.size      = SK_File_GetSize   (new_path.c_str ());
    new_path.clear();

    // We're going to stream the cover in asynchronously on this thread
    HANDLE hWorkerThread = (HANDLE)
    _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
    {
      SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_TIME_CRITICAL);

      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIV_ImageWorker");

      thread_s* _data = static_cast<thread_s*>(var);

      CoInitializeEx (nullptr, 0x0);

      PLOG_DEBUG << "SKIV_ImageWorker thread started!";

      PLOG_INFO  << "Streaming game cover asynchronously...";

      int queuePos = getTextureLoadQueuePos();
      //PLOG_VERBOSE << "queuePos = " << queuePos;

      // Animated images are stopped between frames once another image is to be shown instead
      bool success = LoadLibraryTexture ( _data->image, [queuePos](void) -> bool { return textureLoadQueueLength.load() == queuePos; } );

      PLOG_VERBOSE << "_pRawTexSRV = "        << _data->image.pRawTexSRV;

      int currentQueueLength = textureLoadQueueLength.load();

      if (currentQueueLength == queuePos)
      {
        if (success)
          PLOG_VERBOSE << "Queue position is live, and texture was successfully loaded!";
        else
          PLOG_WARNING << "Queue position is live, but texture failed to load properly...";

        if (success)
          _LogColorSource (_data->image);

        cover.file_info         = _data->image.file_info;
        cover.bpc               = _data->image.bpc;
        cover.channels          = _data->image.channels;
        cover.width             = _data->image.width;
        cover.height            = _data->image.height;
        cover.zoom              = _data->image.zoom;
        cover.uv0               = _data->image.uv0;
        cover.uv1               = _data->image.uv1;
        cover.pRawTexSRV        = _data->image.pRawTexSRV;
        cover.pGamutCoverageSRV = _data->image.pGamutCoverageSRV;
        cover.pGamutCoverageUAV = _data->image.pGamutCoverageUAV;
        cover.light_info        = _data->image.light_info;
        cover.colorimetry       = _data->image.colorimetry;
        cover.is_hdr            = _data->image.is_hdr;
        cover.is_wcg            = _data->image.is_wcg;
        cover.is_dds            = _data->image.is_dds;
        cover.anim              = std::move (_data->image.anim);
        cover.color_source      = std::move (_data->image.color_source);

        // Parent folder (used for the directory watch)
        std::filesystem::path path       = SKIF_Util_NormalizeFullPath (cover.file_info.path);
        cover.file_info.folder_path      = path.parent_path().wstring();
        cover.file_info.folder_path_utf8 = SK_WideCharToUTF8 (cover.file_info.folder_path);
        cover.file_info.filename         = path.filename()   .wstring();
        cover.file_info.filename_utf8    = SK_WideCharToUTF8 (cover.file_info.filename);
        cover.file_info.size             = SK_File_GetSize   (cover.file_info.path.c_str ());

        extern ImVec2 SKIV_ResizeApp;
        SKIV_ResizeApp.x = cover.width;
        SKIV_ResizeApp.y = cover.height;

        // Indicate that we have stopped loading the cover
        imageLoading.store (false);

        // Force a refresh when the cover has been swapped in
        PostMessage (SKIF_Notify_hWnd, WM_SKIF_IMAGE, 0x0, static_cast<LPARAM> (success));
      }

      else if (_data->image.pRawTexSRV.p        != nullptr ||
               _data->image.pGamutCoverageSRV.p != nullptr ||
               _data->image.pGamutCoverageUAV.p != nullptr ||
               _data->image.anim                != nullptr)
      {
        SKIV_ReleaseAnimation (_data->image.anim);

        if (_data->image.pRawTexSRV.p != nullptr)
        {
          PLOG_DEBUG << "Texture is late! (" << queuePos << " vs " << currentQueueLength << ")";
          PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << _data->image.pRawTexSRV.p << " to be released";;
          SKIF_ResourcesToFree.push(_data->image.pRawTexSRV.p);
          _data->image.pRawTexSRV.p = nullptr;
        }

        if (_data->image.pGamutCoverageSRV.p != nullptr)
        {
          PLOG_DEBUG << "Texture is late! (" << queuePos << " vs " << currentQueueLength << ")";
          PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << _data->image.pGamutCoverageSRV.p << " to be released";;
          SKIF_ResourcesToFree.push(_data->image.pGamutCoverageSRV.p);
          _data->image.pGamutCoverageSRV.p = nullptr;
        }

        if (_data->image.pGamutCoverageUAV.p != nullptr)
        {
          PLOG_DEBUG << "Texture is late! (" << queuePos << " vs " << currentQueueLength << ")";
          PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << _data->image.pGamutCoverageUAV.p << " to be released";;
          SKIF_ResourcesToFree.push(_data->image.pGamutCoverageUAV.p);
          _data->image.pGamutCoverageUAV.p = nullptr;
        }
      }

      delete _data;

      PLOG_INFO  << "Finished streaming image asynchronously...";
      PLOG_DEBUG << "SKIV_ImageWorker thread stopped!";
      return 0;
    }, data, 0x0, nullptr);

    bool threadCreated = (hWorkerThread != NULL);

    if (threadCreated) // We don't care about how it goes so the handle is unneeded
      CloseHandle (hWorkerThread);
    else // Someting went wrong during thread creation, so free up the memory we allocated earlier
      delete data;
  }

#pragma endregion

  if (OpenFileDialog == PopupState_Open)
  {
    OpenFileDialog = PopupState_Opened;

    static const filterspec_s filters = CreateFILTERSPEC (Open);

#ifdef _DEBUG
    for (auto& filter : filters.filterSpec)
      PLOG_VERBOSE << std::wstring (filter.pszName) << ": " << std::wstring (filter.pszSpec);
#endif

    LPWSTR pwszFilePath = NULL;
    HRESULT hr          = // COMDLG_FILTERSPEC{ L"Images", L"*.png;*.jpg;*.jpeg;*.webp;*.psd;*.bmp;*.jxr;*.hdr;*.avif" }
      SK_FileOpenDialog (&pwszFilePath, filters.filterSpec.data(), static_cast<UINT> (filters.filterSpec.size()), SKIF_ImGui_hWnd, FOS_FILEMUSTEXIST, FOLDERID_Pictures);

    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
      // If cancelled, do nothing
    }

    else if (SUCCEEDED(hr))
    {
      dragDroppedFilePath = pwszFilePath;
    }

    OpenFileDialog = PopupState_Closed;
  }


  if (SaveFileDialog == PopupState_Open)
  {
    SaveFileDialog = PopupState_Opened;

    const filterspec_s filters = CreateFILTERSPEC ((cover.is_hdr ? Save_HDR : Save_SDR));

#ifdef _DEBUG
    for (auto& filter : filters.filterSpec)
      PLOG_VERBOSE << std::wstring (filter.pszName) << ": " << std::wstring (filter.pszSpec);
#endif

    wchar_t               wszCoverName [MAX_PATH + 2] = { };
    wcsncpy (             wszCoverName, cover.file_info.filename.c_str(), MAX_PATH);
    PathRemoveExtensionW (wszCoverName);

    const wchar_t *wszDefaultExtension =
      cover.is_hdr ? defaultHDRFileExt.c_str ()
                   : defaultSDRFileExt.c_str ();

    LPWSTR pwszFilePath = NULL;
    HRESULT hr          = // COMDLG_FILTERSPEC{ L"Images", L"*.png;*.jpg;*.jpeg;*.webp;*.psd;*.bmp;*.jxr;*.hdr;*.avif" }
      SK_FileSaveDialog (&pwszFilePath, wszCoverName, wszDefaultExtension, filters.filterSpec.data(), static_cast<UINT> (filters.filterSpec.size()), SKIF_ImGui_hWnd, FOS_STRICTFILETYPES|FOS_FILEMUSTEXIST|FOS_OVERWRITEPROMPT|FOS_DONTADDTORECENT, FOLDERID_Pictures, cover.file_info.folder_path.c_str());

    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
      // If cancelled, do nothing
    }

    else if (SUCCEEDED(hr))
    {
      hr = E_UNEXPECTED;

      auto pDevice =
        SKIF_D3D11_GetDevice ();

      if (pDevice && cover.pRawTexSRV.p != nullptr)
      {
        {
          // An animated image: its current frame
          DirectX::ScratchImage                          captured_img;
          if (SUCCEEDED (SKIV_CaptureImage (cover, captured_img)))
          {
            if (cover.is_hdr)
            {
              hr =
                SKIV_Image_SaveToDisk_HDR (*captured_img.GetImages (), pwszFilePath);
            }

            else
            {
              hr =
                SKIV_Image_SaveToDisk_SDR (*captured_img.GetImages (), pwszFilePath, false);
            }
          }
        }
      }

      if (FAILED (hr))
      {
        // Crap...
        ImGui::InsertNotification (
        {
          ImGuiToastType::Error,
          15000,
          "File Save", "Failed to Save '%ws', HRESULT=%x",
          pwszFilePath, hr
        });
      }
    }

    SaveFileDialog = PopupState_Closed;
  }

  if (ExportSDRDialog == PopupState_Open)
  {
    ExportSDRDialog = PopupState_Opened;

    static const filterspec_s filters = CreateFILTERSPEC (Save_SDR);

#ifdef _DEBUG
    for (auto& filter : filters.filterSpec)
      PLOG_VERBOSE << std::wstring (filter.pszName) << ": " << std::wstring (filter.pszSpec);
#endif

    wchar_t               wszCoverName [MAX_PATH + 2] = { };
    wcsncpy (             wszCoverName, cover.file_info.filename.c_str(), MAX_PATH);
    PathRemoveExtensionW (wszCoverName);

    const wchar_t *wszDefaultExtension =
               defaultSDRFileExt.c_str ();

    LPWSTR pwszFilePath = NULL;
    HRESULT hr          = // COMDLG_FILTERSPEC{ L"Images", L"*.png;*.jpg;*.jpeg;*.webp;*.psd;*.bmp;*.jxr;*.hdr;*.avif" }
      SK_FileSaveDialog (&pwszFilePath, wszCoverName, wszDefaultExtension, filters.filterSpec.data(), static_cast<UINT> (filters.filterSpec.size()), SKIF_ImGui_hWnd, FOS_STRICTFILETYPES|FOS_FILEMUSTEXIST, FOLDERID_Pictures, cover.file_info.folder_path.c_str());

    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
      // If cancelled, do nothing
    }

    else if (SUCCEEDED(hr))
    {
      hr = E_UNEXPECTED;

      auto pDevice =
        SKIF_D3D11_GetDevice ();

      if (pDevice && cover.pRawTexSRV.p != nullptr)
      {
        CComPtr <ID3D11DeviceContext>  pDevCtx;
        pDevice->GetImmediateContext (&pDevCtx);

        if (pDevCtx)
        {
          CComPtr <ID3D11Resource>        pCoverRes;
          cover.pRawTexSRV->GetResource (&pCoverRes.p);

          DirectX::ScratchImage                                                captured_img;
          if (SUCCEEDED (DirectX::CaptureTexture (pDevice, pDevCtx, pCoverRes, captured_img)))
          {

            if (cover.is_hdr)
            {
              hr =
                SKIV_Image_SaveToDisk_SDR (*captured_img.GetImages (), pwszFilePath, false);
            }

            else
            {
              // WTF? It's already SDR... oh well, save it anyway
              hr =
                SKIV_Image_SaveToDisk_SDR (*captured_img.GetImages (), pwszFilePath, false);
            }
          }
        }
      }

      if (FAILED (hr))
      {
        // Crap...
        ImGui::InsertNotification (
        {
          ImGuiToastType::Error,
          15000,
          "SDR Export", "Failed to Export SDR copy to '%ws', HRESULT=%x",
          pwszFilePath, hr
        });
      }
    }

    ExportSDRDialog = PopupState_Closed;
  }


  if (! tryingToLoadImage && ! tryingToDownImage)
  {
    if (ImGui::IsKeyPressed (ImGuiKey_RightArrow) ||
        ImGui::IsKeyPressed (ImGuiKey_GamepadR1))
    {
      dragDroppedFilePath = _current_folder.fileList.nextImage ( );
    }

    else if (ImGui::IsKeyPressed (ImGuiKey_LeftArrow) ||
             ImGui::IsKeyPressed (ImGuiKey_GamepadL1))
    {
      dragDroppedFilePath = _current_folder.fileList.prevImage ( );
    }

    // Animated images: Space pauses / resumes, and , / . step a frame back / ahead (pausing it)
    else if (cover.anim != nullptr && ! ImGui::GetIO ( ).WantTextInput && ! ImGui::IsPopupOpen (nullptr, ImGuiPopupFlags_AnyPopupId))
    {
      const DWORD now = SKIF_Util_timeGetTime ( );

      if (ImGui::IsKeyPressed (ImGuiKey_Space, false))
      {
        if (cover.anim->paused)
          cover.anim->play  (now);
        else
          cover.anim->pause (now);
      }

      else if (ImGui::IsKeyPressed (ImGuiKey_Period))
        cover.anim->step (now,  1);

      else if (ImGui::IsKeyPressed (ImGuiKey_Comma))
        cover.anim->step (now, -1);
    }
  }

  if (! dragDroppedFilePath.empty())
  {
    // First position is a quotation mark -- we need to strip those
    if (dragDroppedFilePath.find(L"\"") == 0)
      dragDroppedFilePath = dragDroppedFilePath.substr(1, dragDroppedFilePath.find(L"\"", 1) - 1) + dragDroppedFilePath.substr(dragDroppedFilePath.find(L"\"", 1) + 1, std::wstring::npos);

    bool         isURL = false;
    std::wstring filename;
    std::wstring file_ext;

    auto _ProcessPath = [&](const std::wstring& inFilePath)
    {
      isURL    = PathIsURL (inFilePath.c_str());
      filename = inFilePath;

      if (isURL)
      {
        skif_get_web_uri_t cracked = SKIF_Util_CrackWebUrl (filename);

        if (cracked.wszHostPath[0] != '\0')
          filename = std::wstring (cracked.wszHostPath);
      }

      const std::filesystem::path p = std::filesystem::path(filename.data());
      filename = p.filename().wstring();
      file_ext = p.extension().wstring();
    };

    // First round
    _ProcessPath (dragDroppedFilePath);

    // .URL files
    if (SKIF_Util_ToLowerW (file_ext) == L".url" && PathFileExists (dragDroppedFilePath.c_str()))
    {
      PLOG_VERBOSE << "Parsing .url file...";

      std::ifstream fs (dragDroppedFilePath, std::ios::in | std::ios::binary);

      if (fs.is_open())
      {
        std::string html;

        while (std::getline (fs, html))
        {
          std::string htmlLower = SKIF_Util_ToLower (html);
          std::string split1    = R"(url=)";  // Split at 'url='

          // Split 1 (trim before URL= element)
          if (htmlLower.find (split1) != std::wstring::npos)
          {
            html = html.substr (htmlLower.find (split1) + split1.length());

            if (! html.empty())
            {
              fb::HtmlCoder html_decoder;
              html_decoder.decode(html);

              PLOG_VERBOSE << "Extracted link: " << html;
              dragDroppedFilePath = SK_UTF8ToWideChar (html);

              break;
            }
          }
        }

        fs.close();
      }
    }

    // Second round
    _ProcessPath (dragDroppedFilePath);

    PLOG_VERBOSE << "New " << ((isURL) ? "URL" : "file") << " drop was given; extension: " << file_ext << ", path: " << dragDroppedFilePath;

    // Images + URLs
    if (isExtensionSupported (file_ext))
    {
      if (isURL)
        tryingToDownImage = SaveTempImage (dragDroppedFilePath, filename);

      else
      {
        dragDroppedFilePath = SKIF_Util_NormalizeFullPath (dragDroppedFilePath);
        new_path     = dragDroppedFilePath;
      }
    }

    // Unsupported files
    else
    {
      constexpr char* error_title =
        "Unsupported file format";

      std::string error_label =
                       "Use one of the following supported formats:\n"
                       "\n";
      for (auto& type : supported_formats)
      {
        static std::string prev_ext;
        std::string type_ext = SK_WideCharToUTF8 (type.file_extensions[0]);

        if (prev_ext == type_ext) // Filter out duplicates (e.g. .tiff and .gif)
          continue;

        error_label += "   *" + type_ext + "\n";

        prev_ext = type_ext;
      }
      error_label   += "\n"
                       "Animated images are supported as APNG, GIF, WebP, and AVIF.";

      SKIF_ImGui_InfoMessage (error_title, error_label);
    }

    dragDroppedFilePath.clear();
  }


  // START FADE/DIM LOGIC

  // Every >15 ms, increase/decrease the cover fade effect (makes it frame rate independent)
  static DWORD timeLastTick;
  bool         incTick = false;

  // Fade in/out transition
  if (_registry.bCrossFade)
  {
    // Fade in the new cover
    if (fAlpha < 1.0f && cover.pRawTexSRV.p != nullptr
        && cover_old.pRawTexSRV.p == nullptr) // But only when the old has faded out (fixes sudden image scroll pos reset)
    {
      if (current_time - timeLastTick > 15)
      {
        fAlpha += 0.05f;
        incTick = true;
      }

      imageFadeActive = true;
    }

    // Fade out the old one
    if (fAlphaPrev > 0.0f && cover_old.pRawTexSRV.p != nullptr)
    {
      if (current_time - timeLastTick > 15)
      {
        fAlphaPrev -= 0.05f;
        incTick     = true;
      }

      imageFadeActive = true;
    }
  }

  // Dim covers

  if (_registry.iDarkenImages == 2)
  {
    if (isImageHovered && fTint < 1.0f)
    {
      if (current_time - timeLastTick > 15)
      {
        fTint = fTint + 0.01f;
        incTick = true;
      }

      imageFadeActive = true;
    }

    else if (! isImageHovered && fTint > fTintMin)
    {
      if (current_time - timeLastTick > 15)
      {
        fTint = fTint - 0.01f;
        incTick = true;
      }

      imageFadeActive = true;
    }
  }

  // Increment the tick
  if (incTick)
    timeLastTick = current_time;

  // END FADE/DIM LOGIC

  // In case of a device reset, unload all currently loaded textures
  if (invalidatedDevice == 1)
  {   invalidatedDevice  = 2;

    SKIV_ReleaseAnimation (cover.anim);
    SKIV_ReleaseAnimation (cover_old.anim);

    if (cover.pRawTexSRV.p != nullptr)
    {
      SKIF_ResourcesToFree.push(cover.pRawTexSRV.p);
      cover.pRawTexSRV.p = nullptr;
    }

    if (cover_old.pRawTexSRV.p != nullptr)
    {
      SKIF_ResourcesToFree.push(cover_old.pRawTexSRV.p);
      cover_old.pRawTexSRV.p = nullptr;

      if (cover_old.pGamutCoverageSRV.p != nullptr)
      {
        SKIF_ResourcesToFree.push(cover_old.pGamutCoverageSRV.p);
        cover_old.pGamutCoverageSRV.p = nullptr;
      }

      if (cover_old.pGamutCoverageUAV.p != nullptr)
      {
        SKIF_ResourcesToFree.push(cover_old.pGamutCoverageUAV.p);
        cover_old.pGamutCoverageUAV.p = nullptr;
      }
    }

    // Trigger a refresh of the cover
    loadImage = true;
  }


  // XXX: There's probably a better place to put this, I was lazy.
  //
  if (ImGui::GetIO ().KeyCtrl && ImGui::GetKeyData (ImGuiKey_C)->DownDuration == 0.0f)
  {
    wantCopyToClipboard = true;
  }
}
