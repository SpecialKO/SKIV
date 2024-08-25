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

#include <tabs/viewer.h>
#include "../../version.h"

#include <wmsdk.h>
#include <filesystem>
#include <SKIV.h>
#include <utility/utility.h>
#include <utility/skif_imgui.h>
#include <ImGuiNotify.hpp>

#include "DirectXTex.h"
#include <utility/DirectXTexEXR.h>
#include <wincodec.h>

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>

#include <cwctype>
#include <regex>
#include <iostream>
#include <locale>
#include <codecvt>
#include <fstream>
#include <filesystem>
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

#include <jxl/codestream_header.h>
#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#include <jxl/resizable_parallel_runner.h>
#include <jxl/resizable_parallel_runner_cxx.h>
#include <jxl/types.h>

#include <stb_image.h>
#include <html_coder.hpp>
#include <utility/image.h>

#pragma comment (lib, "dxguid.lib")


ImRect copyRect = { 0,0,0,0 };
bool wantCopyToClipboard = false;

thread_local stbi__context::cicp_s SKIV_STBI_CICP;
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
  FileSignature { L"image/jpeg",                { L".jpg", L".jpeg" }, { 0xFF, 0xD8, 0x00, 0x00 },   // JPEG (SOI; Start of Image)
                                                                       { 0xFF, 0xFF, 0x00, 0x00 } }, // JPEG App Markers are masked as they can be all over the place (e.g. 0xFF 0xD8 0xFF 0xED)
  FileSignature { L"image/png",                 { L".png"  },          { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A } },
  FileSignature { L"image/webp",                { L".webp" },          { 0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50 },   // 52 49 46 46 ?? ?? ?? ?? 57 45 42 50
                                                                       { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF } }, // mask
  FileSignature { L"image/bmp",                 { L".bmp"  },          { 0x42, 0x4D } },
  FileSignature { L"image/vnd.ms-photo",        { L".jxr", L".hdp"  }, { 0x49, 0x49, 0xBC } },
  FileSignature { L"image/vnd.adobe.photoshop", { L".psd"  },          { 0x38, 0x42, 0x50, 0x53 } },
  FileSignature { L"image/tiff",                { L".tiff", L".tif" }, { 0x49, 0x49, 0x2A, 0x00 } }, // TIFF: little-endian
  FileSignature { L"image/tiff",                { L".tiff", L".tif" }, { 0x4D, 0x4D, 0x00, 0x2A } }, // TIFF: big-endian
  FileSignature { L"image/gif",                 { L".gif"  },          { 0x47, 0x49, 0x46, 0x38, 0x37, 0x61 } }, // GIF87a
  FileSignature { L"image/gif",                 { L".gif"  },          { 0x47, 0x49, 0x46, 0x38, 0x39, 0x61 } }, // GIF89a
  FileSignature { L"image/vnd.radiance",        { L".hdr"  },          { 0x23, 0x3F, 0x52, 0x41, 0x44, 0x49, 0x41, 0x4E, 0x43, 0x45, 0x0A } }, // Radiance High Dynamic Range image file
  FileSignature { L"image/x-exr",               { L".exr"  },          { 0x76, 0x2F, 0x31, 0x01 } },
  FileSignature { L"image/avif",                { L".avif" },          { 0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x61, 0x76, 0x69, 0x66 },   // ftypavif
                                                                       { 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } }, // ?? ?? ?? ?? 66 74 79 70 61 76 69 66
  FileSignature { L"image/jxl",                 { L".jxl"  },          { 0xFF, 0x0A } },                                                             // Naked
  FileSignature { L"image/jxl",                 { L".jxl"  },          { 0x00, 0x00, 0x00, 0x0C, 0x4A, 0x58, 0x4C, 0x20, 0x0D, 0x0A, 0x87, 0x0A } }, // ISOBMFF-based container
  FileSignature { L"image/vnd-ms.dds",          { L".dds"  },          { 0x44, 0x44, 0x53, 0x20 } },
//FileSignature { L"image/x-targa",             { L".tga"  },          { 0x00, } }, // TGA has no real unique header identifier, so just use the file extension on those
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
//FileSignature { L"image/vnd-ms.dds",          { L".dds"  },          { 0x44, 0x44, 0x53, 0x20 } },
//FileSignature { L"image/x-targa",             { L".tga"  },          { 0x00, } }, // TGA has no real unique header identifier, so just use the file extension on those
};

const std::initializer_list<FileSignature> supported_hdr_encode_formats =
{
  FileSignature { L"image/png",                 { L".png"  },          { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A } },
  FileSignature { L"image/vnd.ms-photo",        { L".jxr"  },          { 0x49, 0x49, 0xBC } },
  FileSignature { L"image/vnd.radiance",        { L".hdr"  },          { 0x23, 0x3F, 0x52, 0x41, 0x44, 0x49, 0x41, 0x4E, 0x43, 0x45, 0x0A } }, // Radiance High Dynamic Range image file
  FileSignature { L"image/x-exr",               { L".exr"  },          { 0x76, 0x2F, 0x31, 0x01 } },
  FileSignature { L"image/jxl",                 { L".jxl"  },          { 0xFF, 0x0A } },
//FileSignature { L"image/avif",                { L".avif" },          { 0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x61, 0x76, 0x69, 0x66 },   // ftypavif
//                                                                     { 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } }, // ?? ?? ?? ?? 66 74 79 70 61 76 69 66
//FileSignature { L"image/vnd-ms.dds",          { L".dds"  },          { 0x44, 0x44, 0x53, 0x20 } },
};

bool isJXLDecoderAvailable (void)
{
  static HMODULE hModJXL        = nullptr;
  static HMODULE hModJXLCMS     = nullptr;
  static HMODULE hModJXLThreads = nullptr;

  static const wchar_t* wszPluginArch =
    SK_RunLHIfBitness ( 64, LR"(x64\)",
                            LR"(x86\)" );

  SK_RunOnce (
  {
    SKIF_RegistrySettings& _registry =
      SKIF_RegistrySettings::GetInstance ();

    std::wstring path_to_sk =
      _registry.regKVPathSpecialK.getData ();

    std::error_code                          ec;
    if (std::filesystem::exists (path_to_sk, ec))
    {
      path_to_sk += LR"(\PlugIns\ThirdParty\Image Codecs\libjxl\)";
      path_to_sk += wszPluginArch;

      std::filesystem::create_directories
                                  (path_to_sk, ec);
      if (std::filesystem::exists (path_to_sk, ec))
      {
        std::wstring path_to_jxl_threads = path_to_sk + L"jxl_threads.dll";
        std::wstring path_to_jxl_cms     = path_to_sk + L"jxl_cms.dll";
        std::wstring path_to_jxl         = path_to_sk + L"jxl.dll";

        if (! std::filesystem::exists (path_to_jxl, ec))
          SKIF_Util_GetWebResource (L"https://sk-data.special-k.info/addon/ImageCodecs/libjxl/x64/jxl.dll",         path_to_jxl);

        if (! std::filesystem::exists (path_to_jxl_cms, ec))
          SKIF_Util_GetWebResource (L"https://sk-data.special-k.info/addon/ImageCodecs/libjxl/x64/jxl_cms.dll",     path_to_jxl_cms);

        if (! std::filesystem::exists (path_to_jxl_threads, ec))
          SKIF_Util_GetWebResource (L"https://sk-data.special-k.info/addon/ImageCodecs/libjxl/x64/jxl_threads.dll", path_to_jxl_threads);

        // JXL depends on CMS to be loaded first

        hModJXLThreads = LoadLibraryW (path_to_jxl_threads.c_str ());
        hModJXLCMS     = LoadLibraryW (path_to_jxl_cms.    c_str ());
        hModJXL        = LoadLibraryW (path_to_jxl.        c_str ());

        if ( hModJXL        != nullptr &&
             hModJXLCMS     != nullptr &&
             hModJXLThreads != nullptr )
        {
          PLOG_INFO << "Loaded JPEG-XL DLLs from: " << path_to_sk;
          return true;
        }
      }
    }

    if (hModJXLThreads == nullptr) hModJXLThreads = LoadLibraryW (L"jxl_threads.dll");
    if (hModJXLCMS     == nullptr) hModJXLCMS     = LoadLibraryW (L"jxl_cms.dll");
    if (hModJXL        == nullptr) hModJXL        = LoadLibraryW (L"jxl.dll");

    if ( hModJXL        != nullptr &&
         hModJXLCMS     != nullptr &&
         hModJXLThreads != nullptr )
    {
      PLOG_INFO << "Loaded JPEG-XL DLLs from default DLL search path";
      return true;
    }
  });

  const bool supported =
    ( hModJXL        != nullptr &&
      hModJXLThreads != nullptr &&
      hModJXLCMS     != nullptr );

  if (! supported)
  {
    ImGuiToast toast = {
      ImGuiToastType::Warning, 3333,
        "JPEG-XL Unsupported because Special K is not Installed",
        "Please install Special K and run SKIV again to view JPEG-XL images.\r\n\t"
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
      // Support for JXL is optional
      if (type.mime_type == L"image/jxl")
      {
        return isJXLDecoderAvailable ();
      }

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
ImRect                 selection_rect    = { };

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

struct image_s {
  struct file_s {
    std::wstring filename         = { }; // Image filename
    std:: string filename_utf8    = { };
    std::wstring folder_path      = { }; // Parent folder path
    std:: string folder_path_utf8 = { };
    std::wstring path             = { }; // Image path (full)
    std:: string path_utf8        = { };
    size_t       size             = 0;
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

  bool         is_hdr      = false;

  CComPtr <ID3D11ShaderResourceView>  pRawTexSRV;
  CComPtr <ID3D11UnorderedAccessView> pGamutCoverageUAV;
  CComPtr <ID3D11ShaderResourceView>  pGamutCoverageSRV;

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
    is_hdr              = other.is_hdr;
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
    is_hdr              = false;
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
extern float           SKIF_ImGui_GlobalDPIScale_Last;
extern std::string     SKIF_StatusBarHelp;
extern std::string     SKIF_StatusBarText;
extern DWORD           invalidatedDevice;
extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;

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
  ImageDecoder_EXR,
  ImageDecoder_HDR,
  ImageDecoder_AVIF // TODO
};

class SK_AutoFile {
public:
  SK_AutoFile (FILE* pFile) : file_ (pFile)
  {
    if (file_ != nullptr)
    {
      auto orig_pos = _ftelli64 (pFile);
                      _fseeki64 (pFile,        0, SEEK_END);
              size_ = _ftelli64 (pFile);
                      _fseeki64 (pFile, orig_pos, SEEK_SET);
    }
  }

  ~SK_AutoFile (void)
  {
    if (file_ != nullptr)
    {
      fclose (std::exchange (file_, nullptr));
    }
  }

  long long getInitialSize (void) const {
    return size_;
  }

private:
  long long size_;
  FILE*     file_;
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

bool
LoadLibraryTexture (image_s& image)
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
  bool need_srgb = false;

  DWORD pre = SKIF_Util_timeGetTime1 ();

  if (image.file_info.path.empty ())
    return false;

  const std::filesystem::path
    imagePath (image.file_info.path.data ());

  std::wstring ext   = SKIF_Util_ToLowerW (imagePath.extension ().wstring ());
  std::string szPath = SK_WideCharToUTF8  (image.file_info.path);

  ImageDecoder decoder = ImageDecoder_None;

  if (ext == L".tga")
    decoder = ImageDecoder_stbi;

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
             (type.mime_type == L"image/jpeg"                ) ? ImageDecoder_stbi : // covers both .jpeg and .jpg
             (type.mime_type == L"image/png"                 ) ? ImageDecoder_stbi : // Use WIC for proper color correction
             (type.mime_type == L"image/bmp"                 ) ? ImageDecoder_stbi :
             (type.mime_type == L"image/vnd.adobe.photoshop" ) ? ImageDecoder_stbi :
             (type.mime_type == L"image/gif"                 ) ? ImageDecoder_stbi :
             (type.mime_type == L"image/vnd.radiance"        ) ? ImageDecoder_HDR  :
           //(type.mime_type == L"image/x-targa"             ) ? ImageDecoder_stbi : // TGA has no real unique header identifier, so just use the file extension on those
             (type.mime_type == L"image/vnd.ms-photo"        ) ? ImageDecoder_WIC  :
             (type.mime_type == L"image/webp"                ) ? ImageDecoder_WIC  :
             (type.mime_type == L"image/tiff"                ) ? ImageDecoder_WIC  :
             (type.mime_type == L"image/avif"                ) ? ImageDecoder_WIC  :
             (type.mime_type == L"image/jxl"                 ) ? ImageDecoder_JXL  :
             (type.mime_type == L"image/vnd-ms.dds"          ) ? ImageDecoder_DDS  :
             (type.mime_type == L"image/x-exr"               ) ? ImageDecoder_EXR  :
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

          if (type.mime_type == L"image/png")
          {
            // XXX: Check for the appropriate chunk
            need_srgb = true;
          }

          break;
        }
      }
    }
  }

  SK_AutoFile _(pImageFile);

  auto _scratchMemory =
    std::make_unique <unsigned char []> (_.getInitialSize ());

  PLOG_ERROR_IF(decoder == ImageDecoder_None) << "Failed to detect file type!";
  PLOG_DEBUG_IF(decoder == ImageDecoder_stbi) << "Using stbi decoder...";
  PLOG_DEBUG_IF(decoder == ImageDecoder_WIC ) << "Using WIC decoder...";
  PLOG_DEBUG_IF(decoder == ImageDecoder_DDS ) << "Using DDS decoder...";
  PLOG_DEBUG_IF(decoder == ImageDecoder_JXL ) << "Using JPEG-XL decoder...";
  PLOG_DEBUG_IF(decoder == ImageDecoder_EXR ) << "Using OpenEXR decoder...";
  PLOG_DEBUG_IF(decoder == ImageDecoder_HDR ) << "Using Radiance HDR decoder...";

  if (decoder == ImageDecoder_None)
    return false;

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
    SKIV_STBI_ResultInfo = { };

#define STBI_FLOAT
#ifdef STBI_FLOAT
    // Check whether the image is a HDR image or not
    image.light_info.isHDR = stbi_is_hdr_from_file (pImageFile);

    PLOG_VERBOSE << "STBI thinks the image is... " << ((image.light_info.isHDR) ? "HDR" : "SDR");

    fseek  (pImageFile,                                 0, SEEK_SET  );
    fread  (_scratchMemory.get (), _.getInitialSize (), 1, pImageFile);
    rewind (pImageFile);

    if (image_sig->mime_type == L"image/png")
    {
      std::string_view     data_view ((const char *)_scratchMemory.get (), _.getInitialSize ());
      if (auto cicp_pos  = data_view.find ("cICP", 0, 4);
               cicp_pos != data_view.npos)
      {
        memcpy (&SKIV_STBI_CICP, &_scratchMemory.get ()[cicp_pos+4], 4);
      }
    }

    float*                pixels = SKIV_STBI_CICP.primaries != 0 ?
                                                         nullptr :
                                   stbi_loadf_from_memory (_scratchMemory.get (), static_cast <int> (_.getInitialSize ()), &width, &height, &channels_in_file, desired_channels);
    typedef float         pixel_size;
    DXGI_FORMAT           dxgi_format = DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT;
#else
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
        dxgi_format = DXGI_FORMAT_R16G16B16A16_FLOAT;

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

      if ( dxgi_format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
          (dxgi_format == DXGI_FORMAT_R16G16B16A16_FLOAT && image.is_hdr))
      {
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

                  outPixels [j] =
                    XMVector3Transform (SKIV_Image_PQToLinear (v), c_Bt2100toscRGB);
                }
              }, img );

              meta.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
              converted   = true;
              succeeded   = true;
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
            DirectX::WIC_FLAGS_FILTER_POINT | DirectX::WIC_FLAGS_DEFAULT_SRGB,
              &meta, img)))
    {
      image.bpc      =
        (int)DirectX::BitsPerColor (meta.format);
      image.channels =
        DirectX::HasAlpha          (meta.format) ? 4 : 3; // 2 and 1 channel images are unsupported for now


      if (image.is_hdr && (image_sig->mime_type == L"image/vnd.ms-photo" ||
                           image_sig->mime_type == L"image/avif"))
      {
        if (DirectX::BitsPerColor (meta.format) < 16)
        {
          PLOG_INFO << "HDR capable image format does not contain HDR pixels... demoting to SDR!";

          image.is_hdr = false;

          if (! DirectX::IsSRGB (meta.format))
            need_srgb  = true;
        }

        else
        {
          image.light_info.isHDR = true;
        }
      }

      // DirectXTex does not handle gamma on 10-bpc PNGs correctly
      if (need_srgb && DirectX::BitsPerColor (meta.format) != 8)
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
      const DXGI_FORMAT final_format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
      DirectX::ScratchImage temp_img = { };

      succeeded = true;

      if (DirectX::IsCompressed (meta.format))
      {
        PLOG_VERBOSE << "Decompressing texture to the intended format...";

        if (FAILED (DirectX::Decompress (*img.GetImage (0, 0, 0), final_format, temp_img)))
        {
          PLOG_ERROR << "Decompression failed!";
          succeeded = false;
        }

        std::swap (img, temp_img);
        meta = img.GetMetadata ( );
      }

      else if (meta.format != final_format)
      {
        PLOG_VERBOSE << "Converting texture to the intended format...";

        if (FAILED (DirectX::Convert (*img.GetImage (0, 0, 0), final_format, DirectX::TEX_FILTER_DEFAULT | DirectX::TEX_FILTER_SRGB, 0.0f, temp_img)))
        {
          PLOG_ERROR << "Conversion failed!";
          succeeded = false;
        }

        std::swap (img, temp_img);
        meta = img.GetMetadata ( );
      }
    }
  }

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

          if (! (bIsHDR10 || bIsRec709Linear))
          {
            PLOG_WARNING << "Encoded image is neither HDR10 nor scRGB...";
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

                    if (bIsRec709Linear)
                    {
                      v =
                        XMVectorMultiply (v, vRelativeToAbsoluteNits);
                    }

                    else if (bIsHDR10)
                    {
                      v =
                        XMVector3Transform (SKIV_Image_PQToLinear (v), c_Bt2100toscRGB);
                    }

                    uint32_t xm_test_rec709 = 0x0,
                             xm_test_hdr    = 0x0;

                    if (XMVectorGreaterOrEqualR (&xm_test_rec709, v, g_XMZero);
                        XMComparisonAnyFalse    ( xm_test_rec709))
                    {
                      wcg = true;
                    }

                    if (XMVectorGreaterR    (&xm_test_hdr, v, g_XMOne);
                        XMComparisonAnyTrue ( xm_test_hdr))
                    {
                      hdr = true;
                    }

                    outPixels [j] = v;
                  }
                }, converted_img )
              )
            )
          {
            DirectX::Convert (*converted_img.GetImages (), DXGI_FORMAT_R16G16B16A16_FLOAT, DirectX::TEX_FILTER_DEFAULT, 0.0f, img);
          }

          image.light_info.isHDR = wcg||hdr;
          image.is_hdr           = wcg||hdr;

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

  if (image.is_hdr)
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
          XMVector3Transform (v, c_from709toXYZ);

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

      if (percent <= 99.92)
      {
        PLOG_INFO << "99.92th percentile luminance: " <<
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
    if (image.is_hdr)
    {
      D3D11_TEXTURE2D_DESC
        texDesc            = { };
        texDesc.BindFlags  = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        texDesc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texDesc.SampleDesc = { .Count   = 1,
                               .Quality = 0 };
        texDesc.Usage      = D3D11_USAGE_DEFAULT;
        texDesc.ArraySize  = 1;
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

    // Remember HDR images read using the WIC encoder
    if ((meta.format == DXGI_FORMAT_R16G16B16A16_FLOAT  ||
         meta.format == DXGI_FORMAT_R32G32B32A32_FLOAT) && decoder == ImageDecoder_WIC)
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

uint32_t
SKIV_Viewer_CycleVisualizationModes (void)
{
  static int32_t last_toast_id = -1;

  if (cover.is_hdr)
  {
    if (++SKIV_HDR_VisualizationId >= SKIV_HDR_NUM_VISUALIZTIONS)
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

  // ** Move this code somewhere more sensible
  //
  // User is requesting to copy the loaded image to clipboard,
  //   let's download it back from the GPU and have some fun!
  if (wantCopyToClipboard)
  {   wantCopyToClipboard = false;

    if (cover.pRawTexSRV.p != nullptr /* && cover.is_hdr */)
    {
      auto
          pDevice = SKIF_D3D11_GetDevice ();
      if (pDevice != nullptr)
      {
        CComPtr <ID3D11DeviceContext>  pDevCtx;
        pDevice->GetImmediateContext (&pDevCtx.p);

        CComPtr <ID3D11Resource>        pTexResource;
        cover.pRawTexSRV->GetResource (&pTexResource.p);

        if (pTexResource.p != nullptr)
        {
          DirectX::ScratchImage                                                     captured_img;
          if (SUCCEEDED (DirectX::CaptureTexture (pDevice, pDevCtx, pTexResource.p, captured_img)))
          {
            if (copyRect.GetArea () != 0)
            {
              const size_t
                x      = static_cast <size_t> (std::max (0.0f, copyRect.Min.x)),
                y      = static_cast <size_t> (std::max (0.0f, copyRect.Min.y)),
                width  = static_cast <size_t> (std::max (0.0f, copyRect.GetWidth  ())),
                height = static_cast <size_t> (std::max (0.0f, copyRect.GetHeight ()));

              const DirectX::Rect
                src_rect (x,y, width,height);

              DirectX::ScratchImage
                             subrect;
              if (SUCCEEDED (subrect.Initialize2D   ( captured_img.GetMetadata ().format, width, height, 1, 1)) &&
                  SUCCEEDED (DirectX::CopyRectangle (*captured_img.GetImages   (), src_rect,
                                                                                   *subrect.GetImages (), DirectX::TEX_FILTER_DEFAULT, 0, 0)))
              {
                if (SKIV_Image_CopyToClipboard (subrect.GetImages (), true, cover.is_hdr))
                {
                  ImGui::InsertNotification (
                    {
                      ImGuiToastType::Info,
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
              if (SKIV_Image_CopyToClipboard (captured_img.GetImages (), false, cover.is_hdr))
              {
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

      // Set up the current one to be released
      cover_old = cover;
      cover.reset();

      fAlphaPrev          = (_registry.bFadeCovers) ? fAlpha   : 0.0f;
      fAlpha              = (_registry.bFadeCovers) ?   0.0f   : 1.0f;

      // Reset the title of the main app window
      if (SKIF_ImGui_hWnd != NULL)
        ::SetWindowText (SKIF_ImGui_hWnd, SKIV_WINDOW_TITLE_SHORT_W);
    }
  };

#pragma region Initialization

  SK_RunOnce (fAlpha = (_registry.bFadeCovers) ? 0.0f : 1.0f);

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

  // Monitor the current image folder
  struct {
    std::wstring              orig_path; // Holds a cached copy of cover.path
    std::wstring              filename;  // Image filename
    std::wstring              path;      // Parent folder path
    std:: string              path_utf8;
    SKIF_DirectoryWatch       watch;
    std::vector<std::wstring> fileList;
    unsigned int              fileListIndex = 0;

    void reset (void)
    {
      PLOG_VERBOSE << "reset _current_folder!";

      orig_path.clear();
      filename.clear();
      path.clear();
      path_utf8.clear();
      fileList.clear();
      fileListIndex = 0;
      watch.reset();
    }

    std::wstring nextImage (void)
    {
      if (fileList.size() == 0 || fileListIndex == fileList.size() - 1)
        return L"";

      fileListIndex++;
      fileListIndex %= fileList.size();
      return (path + LR"(\)" + fileList[fileListIndex]);
    }

    std::wstring prevImage (void)
    {
      if (fileList.size() == 0 || fileListIndex == 0)
        return L"";

      fileListIndex--;
      fileListIndex %= fileList.size();
      return (path + LR"(\)" + fileList[fileListIndex]);
    }

    // Find the position of the image in the current folder
    void findFileIndex (void)
    {
      // Set the index to the proper position
      fileListIndex = 0;
      for (auto& file : fileList)
      {
        if (filename != file)
          fileListIndex++;
        else
          break;
      }
    }

    // Retrieve all files in the folder, and identify our current place among them...
    void updateFolderData (void)
    {
      HANDLE hFind        = INVALID_HANDLE_VALUE;
      WIN32_FIND_DATA ffd = { };
      fileList.clear();

      PLOG_DEBUG << "Discovering ... " << (path + LR"(\*.*)");

      hFind = 
        FindFirstFileExW ((path + LR"(\*.*)").c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);

      if (INVALID_HANDLE_VALUE != hFind)
      {
        fileList.push_back (ffd.cFileName);

        while (FindNextFile (hFind, &ffd))
          fileList.push_back (ffd.cFileName);

        FindClose (hFind);
      }

      if (! fileList.empty())
      {
        std::vector<std::wstring> filtered;

        // Filter out unsupported file formats using their file extension
        for (auto& file : fileList)
          if (isExtensionSupported (std::filesystem::path(file).extension().wstring()))
            filtered.push_back (file);

        fileList = filtered;

        if (! fileList.empty())
        {
          std::sort (fileList.begin(),
                     fileList.end  (), 
            []( const std::wstring& a,
                const std::wstring& b ) -> int
            {
              return StrCmpLogicalW (a.c_str(), b.c_str()) < 0;
            }
          );

          findFileIndex ( );
        }
      }

      PLOG_DEBUG << "Found " << fileList.size() << " supported images in the folder.";
    }
  } static _current_folder;

  // Do not clear when we are loading an image (so as to not process the same folder constantly)
  if (! loadImage && ! tryingToLoadImage)
  {
    static DWORD dwLastSignaled = 0;

    // Identify when an image has been closed
    if (cover.file_info.path.empty())
    {
      if (! _current_folder.path.empty())
        _current_folder.reset();
    }

    // Identify when we're dealing with a whole new folder
    if (cover.file_info.folder_path != _current_folder.path)
    {
      _current_folder.reset();
      
      _current_folder.orig_path  = cover.file_info.path;
      std::filesystem::path path = SKIF_Util_NormalizeFullPath (cover.file_info.path);
      _current_folder.filename   = path.filename().wstring();
      _current_folder.path       = path.parent_path().wstring();
      _current_folder.path_utf8  = SK_WideCharToUTF8 (_current_folder.path);

      PLOG_VERBOSE << "Watching the folder... " << _current_folder.path;

      // This triggers a new updateFolderData() run below
      dwLastSignaled = 1;
    }

    // Identify when a new file from the same folder has been dropped
    if (cover.file_info.path != _current_folder.orig_path)
    {
      _current_folder.orig_path  = cover.file_info.path;
      std::filesystem::path path = SKIF_Util_NormalizeFullPath (cover.file_info.path);
      _current_folder.filename   = path.filename().wstring();
      _current_folder.findFileIndex ( );
    }

    // Identify when the folder was changed outside of the app
    if (_current_folder.watch.isSignaled (_current_folder.path))
    {
      dwLastSignaled = SKIF_Util_timeGetTime();
      PLOG_VERBOSE << "_current_folder.watch was signaled! Delay checking the folder for another 500ms...";
    }

    if (dwLastSignaled != 0 && dwLastSignaled + 500 < SKIF_Util_timeGetTime())
    {
      _current_folder.updateFolderData();
      dwLastSignaled = 0;
    }
  }

  // Only apply changes to the scaling method if we actually have an image loaded
  if (cover.pRawTexSRV.p != nullptr)
  {
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

  static const ImVec2 hdr_uv0 (-1024.0f, -1024.0f);
  static const ImVec2 hdr_uv1 (-2048.0f, -2048.0f);
  
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
  
    SKIF_ImGui_OptImage  (cover_old.pRawTexSRV.p,
                                                      ImVec2 (sizeCover_old.x,
                                                              sizeCover_old.y),
                                    cover_old.light_info.isHDR ? hdr_uv0 : cover_old.uv0, // Top Left coordinates
                                    cover_old.light_info.isHDR ? hdr_uv1 : cover_old.uv1, // Bottom Right coordinates
                                    (_registry._StyleLightMode) ? ImVec4 (1.0f, 1.0f, 1.0f, fGammaCorrectedTint * AdjustAlpha (fAlphaPrev))  : ImVec4 (fTint, fTint, fTint, fAlphaPrev) // Alpha transparency
    );
  
    ImGui::SetCursorPos (originalPos);

    fading = true;
  }

  bIsHDR =
    cover.is_hdr;

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

  SKIF_ImGui_OptImage  (cover.pRawTexSRV.p,
                                                    ImVec2 (sizeCover.x,
                                                            sizeCover.y),
                                  cover.light_info.isHDR ? hdr_uv0 : cover.uv0, // Top Left coordinates
                                  cover.light_info.isHDR ? hdr_uv1 : cover.uv1, // Bottom Right coordinates
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
    ImGui::BeginChild     ("###ImageDetails", ImVec2 (0, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ((_registry.bUIBorders) ? ImGuiChildFlags_Border : ImGuiChildFlags_None), ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor  ( );

    // Monospace font
    ImGui::PushFont       (fontConsolas);

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
                                     (cover.is_hdr) ? "HDR" : "SDR",
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
        ImGui::PushFont (fontConsolas);
        ImGui::EndGroup             ();
      }
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

      ImGui::PushFont (fontConsolas);

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

  if (ImGui::BeginPopup   ("ContextMenu", ImGuiWindowFlags_NoMove))
  {
    ContextMenu = PopupState_Opened;
    constexpr char spaces[] = { "\u0020\u0020\u0020\u0020" };

    ImGui::PushStyleColor (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));

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
      if (//cover.is_hdr &&
          SKIF_ImGui_MenuItemEx2 ("Copy",          ICON_FA_CLIPBOARD,   ImGui::GetStyleColorVec4(ImGuiCol_Text),      "Ctrl+C"))
      {
        wantCopyToClipboard = true;
      }
      if (SKIF_ImGui_MenuItemEx2 ("Close", 0,                           ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), "Ctrl+W"))
        _SwapOutCover ();

      // Image scaling

      ImGui::Separator ( );

      ImGui::PushID ("#ImageScaling");

      if (SKIF_ImGui_BeginMenuEx2 ("Scaling", ICON_FA_PANORAMA))
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

      if (cover.is_hdr)
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

          if (SKIF_ImGui_MenuItemEx2 ("Luminance Heatmap", ICON_FA_CIRCLE_RADIATION, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Yellow), spaces, &bHeatmap))
          {
            _ResetSelection (bHeatmap);
            SKIV_HDR_VisualizationId = SKIV_HDR_VISUALIZTION_HEATMAP;
          }

          if (SKIF_ImGui_MenuItemEx2 ("Gamut Coverage", ICON_FA_PALETTE, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Icon), spaces, &bGamut))
          {
            _ResetSelection (bGamut);
            SKIV_HDR_VisualizationId = SKIV_HDR_VISUALIZTION_GAMUT;
          }

          if (SKIF_ImGui_MenuItemEx2 ("SDR Grayscale", ICON_FA_CIRCLE_HALF_STROKE, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Icon), spaces, &bSDR))
          {
            _ResetSelection (bSDR);
            SKIV_HDR_VisualizationId = SKIV_HDR_VISUALIZTION_SDR;
          }

          ImGui::EndMenu ( );
        }

        ImGui::PopID ( ); // #HDRVisualization
      }
      
      if (SKIF_ImGui_MenuItemEx2 ("Details", ICON_FA_BARCODE, ImGui::GetStyleColorVec4 (ImGuiCol_Text), "Ctrl+D", &_registry.bImageDetails))
        _registry.regKVImageDetails.putData (_registry.bImageDetails);

      ImGui::Separator       ( );

      if (! cover.file_info.path.empty() && SKIF_ImGui_MenuItemEx2 ("Browse Folder", ICON_FA_FOLDER_OPEN, ImColor(255, 207, 72), "Ctrl+E"))
        SKIF_Util_FileExplorer_SelectFile (cover.file_info.path.c_str());
    }

    ImGui::Separator       ( );

    if (SKIF_ImGui_MenuItemEx2 ("Settings", ICON_FA_LIST_CHECK))
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
    
      bool success = LoadLibraryTexture ( _data->image );

      PLOG_VERBOSE << "_pRawTexSRV = "        << _data->image.pRawTexSRV;

      int currentQueueLength = textureLoadQueueLength.load();

      if (currentQueueLength == queuePos)
      {
        if (success)
          PLOG_VERBOSE << "Queue position is live, and texture was successfully loaded!";
        else
          PLOG_WARNING << "Queue position is live, but texture failed to load properly...";

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
               _data->image.pGamutCoverageUAV.p != nullptr)
      {
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
      dragDroppedFilePath = _current_folder.nextImage ( );
    }

    else if (ImGui::IsKeyPressed (ImGuiKey_LeftArrow) ||
             ImGui::IsKeyPressed (ImGuiKey_GamepadL1))
    {
      dragDroppedFilePath = _current_folder.prevImage ( );
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
                       "Note that the app has no support for animated images.";

      SKIF_ImGui_InfoMessage (error_title, error_label);
    }

    dragDroppedFilePath.clear();
  }


  // START FADE/DIM LOGIC

  // Every >15 ms, increase/decrease the cover fade effect (makes it frame rate independent)
  static DWORD timeLastTick;
  bool         incTick = false;

  // Fade in/out transition
  if (_registry.bFadeCovers)
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