//
// Copyright 2020 Andon "Kaldaien" Coleman
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

#include "DirectXTex.h"

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
#include <utility/fsutil.h>
#include <atlimage.h>
#include <TlHelp32.h>
#include <gsl/gsl_util>

#include <utility/registry.h>
#include <utility/updater.h>
#include <utility/sk_utility.h>

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

#include <stb_image.h>

enum SKIV_HDR_Visualizations
{
  SKIV_HDR_VISUALIZTION_NONE    = 0,
  SKIV_HDR_VISUALIZTION_HEATMAP = 1,
  SKIV_HDR_VISUALIZTION_GAMUT   = 2,
  SKIV_HDR_VISUALIZTION_SDR     = 3
};

enum SKIV_HDR_VisualizationFlags
{
  SKIV_VIZ_FLAG_SDR_CONSIDER_LUMINANCE  = 0x1,
  SKIV_VIZ_FLAG_SDR_CONSIDER_GAMUT      = 0x2,
  SKIV_VIZ_FLAG_SDR_CONSIDER_OVERBRIGHT = 0x4
};

float SKIV_HDR_SDRWhite = 80.0f;

float SKIV_HDR_GamutHue_Rec709    [4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // White
float SKIV_HDR_GamutHue_DciP3     [4] = { 0.0f, 1.0f, 1.0f, 1.0f }; // Cyan
float SKIV_HDR_GamutHue_Rec2020   [4] = { 0.0f, 1.0f, 0.0f, 1.0f }; // Green
float SKIV_HDR_GamutHue_Ap1       [4] = { 1.0f, 1.0f, 0.0f, 1.0f }; // Yellow
float SKIV_HDR_GamutHue_Ap0       [4] = { 1.0f, 0.0f, 1.0f, 1.0f }; // Magenta
float SKIV_HDR_GamutHue_Undefined [4] = { 1.0f, 0.0f, 0.0f, 1.0f }; // Red

uint32_t SKIV_HDR_VisualizationId       = SKIV_HDR_VISUALIZTION_NONE;
uint32_t SKIV_HDR_VisualizationFlagsSDR = 0xFFFFFFFFU;
float    SKIV_HDR_MaxCLL                = 1.0f;
float    SKIV_HDR_MaxLuminance          = 80.0f;

CComPtr <ID3D11UnorderedAccessView>
         SKIV_HDR_GamutCoverageUAV      = nullptr;
CComPtr <ID3D11ShaderResourceView>
         SKIV_HDR_GamutCoverageSRV      = nullptr;

// Identify file type by reading the file signature
const auto supported_formats =
{
  FileSignature { L"image/jpeg",                { L".jpg", L".jpeg" }, { 0xFF, 0xD8, 0x00, 0x00 },   // JPEG (SOI; Start of Image)
                                                                       { 0xFF, 0xFF, 0x00, 0x00 } }, // JPEG App Markers are masked as they can be all over the place (e.g. 0xFF 0xD8 0xFF 0xED)
  FileSignature { L"image/png",                 { L".png"  },          { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A } },
  FileSignature { L"image/webp",                { L".webp" },          { 0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50 },   // 52 49 46 46 ?? ?? ?? ?? 57 45 42 50
                                                                       { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF } }, // mask
  FileSignature { L"image/bmp",                 { L".bmp"  },          { 0x42, 0x4D } },
  FileSignature { L"image/vnd.ms-photo",        { L".jxr"  },          { 0x49, 0x49, 0xBC } },
  FileSignature { L"image/vnd.adobe.photoshop", { L".psd"  },          { 0x38, 0x42, 0x50, 0x53 } },
  FileSignature { L"image/tiff",                { L".tiff", L".tif" }, { 0x49, 0x49, 0x2A, 0x00 } },    // TIFF: little-endian
  FileSignature { L"image/tiff",                { L".tiff", L".tif" }, { 0x4D, 0x4D, 0x00, 0x2A } }, // TIFF: big-endian
  FileSignature { L"image/vnd.radiance",        { L".hdr"  },          { 0x23, 0x3F, 0x52, 0x41, 0x44, 0x49, 0x41, 0x4E, 0x43, 0x45, 0x0A } }, // Radiance High Dynamic Range image file
  FileSignature { L"image/gif",                 { L".gif"  },          { 0x47, 0x49, 0x46, 0x38, 0x37, 0x61 } }, // GIF87a
  FileSignature { L"image/gif",                 { L".gif"  },          { 0x47, 0x49, 0x46, 0x38, 0x39, 0x61 } }, // GIF89a
  FileSignature { L"image/avif",                { L".avif" },          { 0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x61, 0x76, 0x69, 0x66 },  // ftypavif
                                                                       { 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } } // ?? ?? ?? ?? 66 74 79 70 61 76 69 66
//FileSignature { L"image/x-targa",             { L".tga"  },          { 0x00, } }, // TGA has no real unique header identifier, so just use the file extension on those
};

bool                   loadImage         = false;
bool                   tryingToLoadImage = false;
std::atomic<bool>      imageLoading      = false;
bool                   resetScrollCenter = false;
bool                   newImageLoaded    = false; // Set by the window msg handler when a new image has been loaded

bool                   coverRefresh      = false; // This just triggers a refresh of the cover
std::wstring           coverRefreshPath  = L"";
int                    coverRefreshCount = 0;
int                    numRegular        = 0;
int                    numPinnedOnTop    = 0;

const float fTintMin     = 0.75f;
      float fTint        = 1.0f;
      float fAlpha       = 0.0f;
      float fAlphaPrev   = 1.0f;

PopupState OpenFileDialog  = PopupState_Closed;
PopupState ContextMenu     = PopupState_Closed;

struct image_s {
  struct file_s {
    std::wstring filename      = { }; // Image filename
    std:: string filename_utf8 = { };
    std::wstring path          = { }; // Image path (full)
    std:: string path_utf8     = { };
    std::wstring folder_path   = { }; // Parent folder path
    file_s ( ) { };
  } file_info;

  float        width       = 0.0f;
  float        height      = 0.0f;
  float        zoom        = 1.0f;
  ImVec2       uv0 = ImVec2 (0, 0);
  ImVec2       uv1 = ImVec2 (1, 1);
  ImVec2       avail_size;        // Holds the frame size used (affected by the scaling method)
  ImVec2       avail_size_cache;  // Holds a cached value used to determine if avail_size needs recalculating

  bool         is_hdr      = false;

  CComPtr <ID3D11ShaderResourceView>  pRawTexSRV;
  CComPtr <ID3D11ShaderResourceView>  pTonemappedTexSRV;
  CComPtr <ID3D11UnorderedAccessView> pGamutCoverageUAV;
  CComPtr <ID3D11ShaderResourceView>  pGamutCoverageSRV;

  struct light_info_s {
    float max_cll      = 0.0f;
    char  max_cll_name =  '?';
    float max_nits     = 0.0f;
    float min_nits     = 0.0f;
    float avg_nits     = 0.0f;
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
    width               = other.width;
    height              = other.height;
    zoom                = other.zoom;
    uv0                 = other.uv0;
    uv1                 = other.uv1;
    avail_size          = other.avail_size;
    avail_size_cache    = other.avail_size_cache;
    pRawTexSRV.p        = other.pRawTexSRV.p;
    pTonemappedTexSRV.p = other.pTonemappedTexSRV.p;
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
    width               = 0.0f;
    height              = 0.0f;
    zoom                = 1.0f;
    uv0                 = ImVec2 (0, 0);
    uv1                 = ImVec2 (1, 1);
    avail_size          = { };
    avail_size_cache    = { };
    pRawTexSRV.p        = nullptr;
    pTonemappedTexSRV.p = nullptr;
    pGamutCoverageSRV.p = nullptr;
    pGamutCoverageUAV.p = nullptr;
    is_hdr              = false;
    light_info          = { };
    colorimetry         = { };
  }
};

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
extern void SKIF_Shell_AddJumpList (std::wstring name, std::wstring path, std::wstring parameters, std::wstring directory, std::wstring icon_path, bool bService);

// Functions / Structs

static float
AdjustAlpha (float a)
{
  return std::pow (a, 1.0f / 2.2f );
}

#pragma region LoadTexture

extern CComPtr <ID3D11Device> SKIF_D3D11_GetDevice (bool bWait = true);

enum ImageDecoder {
  ImageDecoder_None,
  ImageDecoder_WIC,
  ImageDecoder_stbi
};

void
LoadLibraryTexture (image_s& image)
{
  // NOT REALLY THREAD-SAFE WHILE IT RELIES ON THESE GLOBAL OBJECTS!
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  CComPtr <ID3D11Texture2D> pRawTex2D;
  CComPtr <ID3D11Texture2D> pTonemappedTex2D;
  CComPtr <ID3D11Texture2D> pGamutCoverageTex2D;
  DirectX::TexMetadata        meta = { };
  DirectX::ScratchImage        img = { };

  bool succeeded = false;
  bool converted = false;

  DWORD pre = SKIF_Util_timeGetTime1();

  if (image.file_info.path.empty())
    return;

  const std::filesystem::path imagePath (image.file_info.path.data());
  std::wstring ext = SKIF_Util_ToLowerW (imagePath.extension().wstring());
  std::string szPath = SK_WideCharToUTF8(image.file_info.path);

  ImageDecoder decoder = ImageDecoder_None;

  if (ext == L".tga")
    decoder = ImageDecoder_stbi;

  if (decoder == ImageDecoder_None)
  {
    static size_t
        maxLength  = 0;
    if (maxLength == 0)
    {
      for (auto& type : supported_formats)
        if (type.signature.size() > maxLength)
          maxLength = type.signature.size();
    }

    std::ifstream file(imagePath, std::ios::binary);

    if (! file)
    {
      PLOG_ERROR << "Failed to open file!";
      return;
    }

    if (file)
    {
      std::vector<char> buffer (maxLength);
      file.read (buffer.data(), maxLength);
      file.close();

      for (auto& type : supported_formats)
      {
        if (SKIF_Util_HasFileSignature (buffer, type))
        {
          PLOG_INFO << "Detected an " << type.mime_type << " image";

          decoder = 
             (type.mime_type == L"image/jpeg"                ) ? ImageDecoder_stbi : // covers both .jpeg and .jpg
             (type.mime_type == L"image/png"                 ) ? ImageDecoder_stbi : // Use WIC for proper color correction
             (type.mime_type == L"image/bmp"                 ) ? ImageDecoder_stbi :
             (type.mime_type == L"image/vnd.adobe.photoshop" ) ? ImageDecoder_stbi :
             (type.mime_type == L"image/gif"                 ) ? ImageDecoder_stbi :
             (type.mime_type == L"image/vnd.radiance"        ) ? ImageDecoder_stbi :
           //(type.mime_type == L"image/x-targa"             ) ? ImageDecoder_stbi : // TGA has no real unique header identifier, so just use the file extension on those
             (type.mime_type == L"image/vnd.ms-photo"        ) ? ImageDecoder_WIC  :
             (type.mime_type == L"image/webp"                ) ? ImageDecoder_WIC  :
             (type.mime_type == L"image/tiff"                ) ? ImageDecoder_WIC  :
           //(type.mime_type == L"image/avif"                ) ? ImageDecoder_???  :
                                                                 ImageDecoder_WIC;   // Not actually being used

          if (type.mime_type == L"image/vnd.radiance" || // .hdr
              type.mime_type == L"image/vnd.ms-photo" || // .jxr
              type.mime_type == L"image/avif")           // .avif
          {
            image.is_hdr = true;
          }

          break;
        }
      }
    }
  }

  PLOG_ERROR_IF(decoder == ImageDecoder_None) << "Failed to detect file type!";
  PLOG_DEBUG_IF(decoder == ImageDecoder_stbi) << "Using stbi decoder...";
  PLOG_DEBUG_IF(decoder == ImageDecoder_WIC ) << "Using WIC decoder...";

  if (decoder == ImageDecoder_None)
    return;

  if (decoder == ImageDecoder_stbi)
  {
    // If desired_channels is non-zero, *channels_in_file has the number of components that _would_ have been
    // output otherwise. E.g. if you set desired_channels to 4, you will always get RGBA output, but you can
    // check *channels_in_file to see if it's trivially opaque because e.g. there were only 3 channels in the source image.

    int width            = 0,
        height           = 0,
        channels_in_file = 0,
        desired_channels = STBI_rgb_alpha;

#define STBI_FLOAT
#ifdef STBI_FLOAT
    // Check whether the image is a HDR image or not
    image.light_info.isHDR = stbi_is_hdr (szPath.c_str());

    float*                pixels = stbi_loadf (szPath.c_str(), &width, &height, &channels_in_file, desired_channels);
    typedef float         pixel_size;
    constexpr DXGI_FORMAT dxgi_format = DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT;
#else
    unsigned char*        pixels = stbi_load  (szPath.c_str(), &width, &height, &channels_in_file, desired_channels);
    typedef unsigned char pixel_size;
    constexpr DXGI_FORMAT dxgi_format = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;
#endif

    if (pixels != NULL)
    {
      meta.width     = width;
      meta.height    = height;
      meta.depth     = 1;
      meta.arraySize = 1;
      meta.mipLevels = 1;
      meta.format    = dxgi_format; // STBI_rgb_alpha
      meta.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

      if (dxgi_format == DXGI_FORMAT_R32G32B32A32_FLOAT)
      {
        // Good grief this is inefficient, let's convert it to something reasonable...
        DirectX::ScratchImage raw_fp32_img;

        if (SUCCEEDED (raw_fp32_img.Initialize2D (meta.format, width, height, 1, 1)))
        {
          size_t   imageSize = width * height * desired_channels * sizeof (pixel_size);
          uint8_t* pDest     = raw_fp32_img.GetImage (0, 0, 0)->pixels;
          memcpy  (pDest, pixels, imageSize);

          // Still overkill for SDR, but we're saving some VRAM...
          const DXGI_FORMAT final_format = DXGI_FORMAT_R16G16B16A16_FLOAT;

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
        size_t   imageSize = width * height * desired_channels * sizeof (pixel_size);
        uint8_t* pDest     = img.GetImage(0, 0, 0)->pixels;
        memcpy  (pDest, pixels, imageSize);

        succeeded = true;
      }
    }

    stbi_image_free (pixels);
  }

  else if (decoder == ImageDecoder_WIC)
  {
    if (SUCCEEDED (
        DirectX::LoadFromWICFile (
          image.file_info.path.c_str (),
            DirectX::WIC_FLAGS_FILTER_POINT | DirectX::WIC_FLAGS_DEFAULT_SRGB,
              &meta, img)))
    {
      succeeded = true;
    }
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

  if (image.pTonemappedTexSRV.p != nullptr)
  {
    extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;
    PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << image.pTonemappedTexSRV.p << " to be released";;
    SKIF_ResourcesToFree.push (image.pTonemappedTexSRV.p);
    image.pTonemappedTexSRV.p = nullptr;
  }

  if (! succeeded)
    return;

  DirectX::ScratchImage* pImg  =   &img;
  DirectX::ScratchImage   converted_img;

  // We don't want single-channel icons, so convert to RGBA
  if (meta.format == DXGI_FORMAT_R8_UNORM)
  {
    if (SUCCEEDED (DirectX::Convert (pImg->GetImages(), pImg->GetImageCount(), pImg->GetMetadata (), DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted_img)))
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
    return;

  pRawTex2D        = nullptr;
  pTonemappedTex2D = nullptr;

  DirectX::ScratchImage normalized_hdr;

  if (image.is_hdr)
  {
    assert (meta.format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
            meta.format == DXGI_FORMAT_R32G32B32A32_FLOAT);

using namespace DirectX;

    static const XMVECTORF32 s_luminance_AP1 =
      { 0.272229f, 0.674082f, 0.0536895f, 0.f };

    static const XMVECTORF32 s_luminance_2020 =
      { 0.2627f,   0.678f,    0.0593f,   0.f };

    static const XMVECTORF32 s_luminance =
      { 0.2126729f, 0.7151522f, 0.0721750f, 0.f };

    static const XMMATRIX c_from2020to709 = // Transposed
    {
      {  1.6604910f, -0.5876411f, -0.0728499f, 0.f },
      { -0.1245505f,  1.1328999f, -0.0083494f, 0.f },
      { -0.0181508f, -0.1005789f,  1.1187297f, 0.f },
      {  0.f,         0.f,         0.f,        1.f }
    };

    static const XMMATRIX c_from709to2020 = // Transposed
    {
      { 0.627225305694944f,  0.0690418812810714f, 0.0163911702607078f, 0.0f },
      { 0.329476882715808f,  0.919605681354755f,  0.0880887513437058f, 0.0f },
      { 0.0432978115892484f, 0.0113524373641739f, 0.895520078395586f,  0.0f },
      { 0.0f,                0.0f,                0.0f,                1.0f }
    };

    static const XMMATRIX c_fromXYZtoDCIP3 = // Transposed
    {
      {  2.7253940305, -0.7951680258,  0.0412418914, 0.0f },
      { -1.0180030062,  1.6897320548, -0.0876390192, 0.0f },
      { -0.4401631952,  0.0226471906,  1.1009293786, 0.0f },
      {  0.0f,          0.0f,          0.0f,         1.0f }
    };

    static const XMMATRIX c_fromXYZtoAP1 = // Transposed
    {
      {  1.6410233797, -0.6636628587,  0.0117218943, 0.0f },
      { -0.3248032942,  1.6153315917, -0.0082844420, 0.0f },
      { -0.2364246952,  0.0167563477,  0.9883948585, 0.0f },
      {  0.0f,          0.0f,          0.0f,         1.0f }
    };

    static const XMMATRIX c_from709toXYZ = // Transposed
    {
      { 0.4123907983303070068359375f,  0.2126390039920806884765625f,   0.0193308182060718536376953125f, 0.0f },
      { 0.3575843274593353271484375f,  0.715168654918670654296875f,    0.119194783270359039306640625f,  0.0f },
      { 0.18048079311847686767578125f, 0.072192318737506866455078125f, 0.950532138347625732421875f,     0.0f },
      { 0.0f,                          0.0f,                           0.0f,                            1.0f }
    };

    static const XMMATRIX c_from709toDCIP3 = // Transposed
    {
      { 0.82246196269989013671875f,    0.03319419920444488525390625f, 0.017082631587982177734375f,  0.0f },
      { 0.17753803730010986328125f,    0.96680581569671630859375f,    0.0723974406719207763671875f, 0.0f },
      { 0.0f,                          0.0f,                          0.91051995754241943359375f,   0.0f },
      { 0.0f,                          0.0f,                          0.0f,                         1.0f }
    };

    static const XMMATRIX c_from709toAP0 = // Transposed
    {
      { 0.4339316189289093017578125f, 0.088618390262126922607421875f, 0.01775003969669342041015625f,  0.0f },
      { 0.3762523829936981201171875f, 0.809275329113006591796875f,    0.109447620809078216552734375f, 0.0f },
      { 0.1898159682750701904296875f, 0.10210628807544708251953125f,  0.872802317142486572265625f,    0.0f },
      { 0.0f,                         0.0f,                           0.0f,                           1.0f }
    };

    static const XMMATRIX c_from709toAP1 = // Transposed
    {
      { 0.61702883243560791015625f,       0.333867609500885009765625f,    0.04910354316234588623046875f,     0.0f },
      { 0.069922320544719696044921875f,   0.91734969615936279296875f,     0.012727967463433742523193359375f, 0.0f },
      { 0.02054978720843791961669921875f, 0.107552029192447662353515625f, 0.871898174285888671875f,          0.0f },
      { 0.0f,                             0.0f,                           0.0f,                              1.0f }
    };

    static const XMMATRIX c_fromAP1to709 = // Transposed
    {
      {  1.70505f, -0.13026f, -0.02400f, 0.0f },
      { -0.62179f,  1.14080f, -0.12897f, 0.0f },
      { -0.08326f, -0.01055f,  1.15297f, 0.0f },
      {  0.0f,      0.0f,      0.0f,     1.0f }
    };

    static const XMMATRIX c_fromAP1toXYZ = // Transposed
    {
      { 0.647507190704345703125f,      0.266086399555206298828125f,   -0.00544886849820613861083984375f,  0.0f },
      { 0.13437913358211517333984375f, 0.67596781253814697265625f,     0.004072095267474651336669921875f, 0.0f },
      { 0.1685695946216583251953125f,  0.057945795357227325439453125f, 1.090434551239013671875f,          0.0f },
      { 0.0f,                          0.0f,                           0.0f,                              1.0f }
    };

    static const XMMATRIX c_fromXYZto709 = // Transposed
    {
      {  3.2409698963165283203125f,    -0.96924364566802978515625f,       0.055630080401897430419921875f, 0.0f },
      { -1.53738319873809814453125f,    1.875967502593994140625f,        -0.2039769589900970458984375f,   0.0f },
      { -0.4986107647418975830078125f,  0.0415550582110881805419921875f,  1.05697154998779296875f,        0.0f },
      {  0.0f,                          0.0f,                             0.0f,                           1.0f }
    };

    XMVECTOR vMaxCLL = g_XMZero;
    XMVECTOR vMaxLum = g_XMZero;
    XMVECTOR vMinLum = g_XMOne;

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

      for (size_t j = 0; j < width; ++j)
      {
        v = *pixels;

        XMVectorSetZ (v, 1.0f);

        vMaxCLL =
          XMVectorMax (v, vMaxCLL);

        vColorXYZ =
          XMVector3Transform (v, c_from709toXYZ);

        xm_test_all = 0x0;

        #define FP16_MIN 0.0000000894069671630859375f

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

        vMaxLum =
          XMVectorMax (vMaxLum, vColorXYZ);

        vMinLum =
          XMVectorMin (vMinLum, vColorXYZ);

        //lumTotal +=
        //  logf ( std::max (0.000001f, 0.000001f + XMVectorGetY (v)) ),
        //++N;

        pixels++;
      }
    } );

    const float fMaxCLL =
      std::max ({
        XMVectorGetX (vMaxCLL),
        XMVectorGetY (vMaxCLL),
        XMVectorGetZ (vMaxCLL)
      });

    XMVECTOR vMaxCLLReplicated =
      XMVectorReplicate (fMaxCLL);

    TransformImage ( pImg->GetImages     (),
                     pImg->GetImageCount (),
                     pImg->GetMetadata   (),
    [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t width, size_t y)
    {
      UNREFERENCED_PARAMETER(y);

      for (size_t j = 0; j < width; ++j)
      {
        XMVECTOR value = inPixels [j];

        outPixels [j] =
          XMVectorDivide (value, vMaxCLLReplicated);
      }
    }, normalized_hdr );

    char cMaxChannel =
      fMaxCLL == XMVectorGetX (vMaxCLL) ? 'R' :
      fMaxCLL == XMVectorGetY (vMaxCLL) ? 'G' :
      fMaxCLL == XMVectorGetZ (vMaxCLL) ? 'B' :
                                          'X';

    // In XYZ space, so Y=Luminance
    float fMaxLum = XMVectorGetY (vMaxLum);
    float fMinLum = XMVectorGetY (vMinLum);

    if (fMinLum < 0.0f)
    {
      PLOG_INFO  << "HDR image contains invalid (non-Rec2020) colors...";

      fMinLum = 0.0f;
    }

    // Not implemented yet, need to implement histogram
    image.light_info.avg_nits = std::numeric_limits <float>::infinity ();

    image.light_info.max_cll      = fMaxCLL;
    image.light_info.max_cll_name = cMaxChannel;
    image.light_info.max_nits     = fMaxLum * 80.0f; // scRGB
    image.light_info.min_nits     = fMinLum * 80.0f; // scRGB
  }

  HRESULT hr =
    DirectX::CreateTexture (pDevice, pImg->GetImages (), pImg->GetImageCount (), meta, (ID3D11Resource **)&pRawTex2D.p);

  if (SUCCEEDED (hr))
  {
    if (image.is_hdr)
    {
      DirectX::CreateTexture (pDevice, normalized_hdr.GetImages (), normalized_hdr.GetImageCount (), normalized_hdr.GetMetadata (), (ID3D11Resource **)&pTonemappedTex2D.p);

      D3D11_TEXTURE2D_DESC
        texDesc            = { };
        texDesc.BindFlags  = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        texDesc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texDesc.SampleDesc = { .Count   = 1,
                               .Quality = 0 };
        texDesc.Usage      = D3D11_USAGE_DEFAULT;
        texDesc.ArraySize  = 1;
        texDesc.Width      = 512;
        texDesc.Height     = 512;

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
    if (meta.format == DXGI_FORMAT_R16G16B16A16_FLOAT && decoder == ImageDecoder_WIC)
      image.light_info.isHDR = true;

    D3D11_SHADER_RESOURCE_VIEW_DESC
    srv_desc                           = { };
    srv_desc.Format                    = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels       = UINT_MAX;
    srv_desc.Texture2D.MostDetailedMip =  0;

    if (pRawTex2D.p != nullptr && SUCCEEDED (pDevice->CreateShaderResourceView (pRawTex2D.p, &srv_desc, &image.pRawTexSRV.p)))
    {
      if (pTonemappedTex2D.p != nullptr)
      {
        pDevice->CreateShaderResourceView (pTonemappedTex2D.p, &srv_desc, &image.pTonemappedTexSRV.p);
      }

      DWORD post = SKIF_Util_timeGetTime1 ( );
      PLOG_INFO << "[Image Processing] Processed image in " << (post - pre) << " ms.";

      // Update the image width/height
      image.width  = static_cast<float>(meta.width);
      image.height = static_cast<float>(meta.height);
    }

    // SRV is holding a reference, this is not needed anymore.
    pRawTex2D           = nullptr;
    pTonemappedTex2D    = nullptr;
    pGamutCoverageTex2D = nullptr;
  }
};

#pragma endregion

#pragma region AspectRatio

ImVec2
GetCurrentAspectRatio (image_s& image)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static int last_scaling = 0;

  ImVec2 avail_size = ImGui::GetContentRegionAvail ( ) / SKIF_ImGui_GlobalDPIScale;

  // Clear any cached data on image changes
  if (image.pRawTexSRV.p == nullptr || image.height == 0 || image.width  == 0)
    return avail_size;

  // Do not recalculate when dealing with an unchanged situation
  if (avail_size   == image.avail_size_cache &&
      last_scaling == _registry.iImageScaling)
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

  // None
  if (_registry.iImageScaling == 0)
  {
    avail_width  = image.width;
    avail_height = image.height;
  }

  // Fill
  else if (_registry.iImageScaling == 1)
  {
    // Workaround to prevent content/frame fighting one another
    if (ImGui::GetScrollMaxY() == 0.0f)
      avail_width -= ImGui::GetStyle().ScrollbarSize;

    if (ImGui::GetScrollMaxX() == 0.0f)
      avail_height -= ImGui::GetStyle().ScrollbarSize;

    // Fill the content area
    if (contentAspectRatio > frameAspectRatio)
      avail_width  = avail_height * contentAspectRatio;
    else // if (contentAspectRatio < frameAspectRatio)
      avail_height = avail_width / contentAspectRatio;

    /* Original (SKIF) implementation that changes the coordinates
     *  This results in a lack of scrollbars, preventing scrolling
    
    // Crop wider aspect ratios by their width
    if (contentAspectRatio > frameAspectRatio)
    {
      float newWidth = avail_height * contentAspectRatio;
      diff.x = (avail_width / newWidth);
      diff.x -= 1.0f;
      diff.x /= 2;

      image.uv0.x = 0.f - diff.x;
      image.uv1.x = 1.f + diff.x;
    }

    // Crop thinner aspect ratios by their height
    else // if (contentAspectRatio < frameAspectRatio)
    {
      float newHeight = avail_width / contentAspectRatio; // image.height / image.width * avail_width
      diff.y = (avail_height / newHeight);
      diff.y -= 1.0f;
      diff.y /= 2;
      
      image.uv0.y = 0.f - diff.y;
      image.uv1.y = 1.f + diff.y;
    }
    */
  }

  // Fit
  else if (_registry.iImageScaling == 2)
  {
    if (contentAspectRatio > frameAspectRatio)
      avail_height = avail_width / contentAspectRatio;
    else
      avail_width  = avail_height * contentAspectRatio;
  }

  // Stretch
  else if (_registry.iImageScaling == 3)
  {
    // Do nothing -- this cases the image to be stretched
  }

  // Cache the current image scaling _and_ reset the scroll center
  if (last_scaling != _registry.iImageScaling)
  {
    last_scaling    = _registry.iImageScaling;
    resetScrollCenter = true;
  }

  PLOG_VERBOSE_IF(! ImGui::IsAnyMouseDown()) // Suppress logging while the mouse is down (e.g. window is being resized)
               << "\n"
               << "Image scaling  : " << _registry.iImageScaling << "\n"
               << "Content region : " << avail_width << "x" << avail_height << "\n"
               << "Image details  :\n"
               << " > resolution  : " << image.width << "x" << image.height << "\n"
               << " > coord 0,0   : " << image.uv0.x << "," << image.uv0.y  << "\n"
               << " > coord 1,1   : " << image.uv1.x << "," << image.uv1.y;

  image.avail_size       = ImVec2(avail_width, avail_height);

  return image.avail_size;
}

#pragma endregion



// Main UI function

void
SKIF_UI_Tab_DrawViewer (void)
{
  extern bool imageFadeActive;

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static DirectX::TexMetadata     meta = { };
  static DirectX::ScratchImage    img  = { };
  static std::wstring new_path = L"";
  static int  tmp_iDarkenImages = _registry.iDarkenImages;
  static image_s cover, cover_old;

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

      if (cover_old.pTonemappedTexSRV.p != nullptr)
      {
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << cover_old.pTonemappedTexSRV.p << " to be released";;
        SKIF_ResourcesToFree.push(cover_old.pTonemappedTexSRV.p);
        cover_old.pTonemappedTexSRV.p = nullptr;
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
  // Ensure we aren't already loading this cover
  if (! new_path.empty() &&
        new_path != cover.file_info.path)
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

    if (cover_old.pTonemappedTexSRV.p != nullptr)
    {
      PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << cover_old.pTonemappedTexSRV.p << " to be released";;
      SKIF_ResourcesToFree.push(cover_old.pTonemappedTexSRV.p);
      cover_old.pTonemappedTexSRV.p = nullptr;
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

  if (newImageLoaded)
  {
    newImageLoaded    = false;
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
        {
          std::wstring ext = std::filesystem::path(file).extension().wstring();

          for (auto& type : supported_formats)
          {
            if (SKIF_Util_HasFileExtension (ext, type))
            {
              filtered.push_back (file);
              break;
            }
          }
        }
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

#pragma endregion

  ImVec2 sizeCover     = GetCurrentAspectRatio (cover)     * ((_registry.iImageScaling == 0) ? 1 : SKIF_ImGui_GlobalDPIScale) * cover.zoom;
  ImVec2 sizeCover_old = GetCurrentAspectRatio (cover_old) * ((_registry.iImageScaling == 0) ? 1 : SKIF_ImGui_GlobalDPIScale) * cover_old.zoom;

  // From now on ImGui UI calls starts being made...

#pragma region GameCover

  static const ImVec2 hdr_uv (-2048.0f, -2048.0f);
  
  static int    queuePosGameCover  = 0;
  static char   cstrLabelLoading[] = "...";
  static char   cstrLabelMissing[] = "Drop an image...";
  char*         pcstrLabel     = nullptr;
  bool          isImageHovered = false;

  ImVec2 originalPos    = ImGui::GetCursorPos ( );
         originalPos.x -= 1.0f * SKIF_ImGui_GlobalDPIScale;

  // A new cover is meant to be loaded, so don't do anything for now...
  if (loadImage)
  { }

  else if (tryingToLoadImage)
    pcstrLabel = cstrLabelLoading;

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
      ImGui::SetCursorPosX ((ImGui::GetContentRegionAvail().x - sizeCover_old.x) * 0.5f);
    if (sizeCover_old.y < ImGui::GetContentRegionAvail().y)
      ImGui::SetCursorPosY ((ImGui::GetContentRegionAvail().y - sizeCover_old.y) * 0.5f);
  
    SKIF_ImGui_OptImage  ((_registry._RendererHDREnabled || (! cover_old.light_info.isHDR)) ? cover_old.pRawTexSRV.p :
                                                                                              cover_old.pTonemappedTexSRV,
                                                      ImVec2 (sizeCover_old.x,
                                                              sizeCover_old.y),
                                    cover_old.light_info.isHDR ? hdr_uv : cover_old.uv0, // Top Left coordinates
                                    cover_old.light_info.isHDR ? hdr_uv : cover_old.uv1, // Bottom Right coordinates
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

  SKIV_HDR_MaxCLL       = cover.light_info.max_cll;
  SKIV_HDR_MaxLuminance = cover.light_info.max_nits;

  if (sizeCover.x < ImGui::GetContentRegionAvail().x)
    ImGui::SetCursorPosX ((ImGui::GetContentRegionAvail().x - sizeCover.x) * 0.5f);
  if (sizeCover.y < ImGui::GetContentRegionAvail().y)
    ImGui::SetCursorPosY ((ImGui::GetContentRegionAvail().y - sizeCover.y) * 0.5f);

  if (_registry._RendererHDREnabled)
    SKIV_HDR_MaxCLL = 1.0f;

  // Display game cover image
  SKIF_ImGui_OptImage  ((_registry._RendererHDREnabled || SKIV_HDR_VisualizationId != SKIV_HDR_VISUALIZTION_NONE || (! cover.light_info.isHDR)) ? cover.pRawTexSRV.p :
                                                                        cover.pTonemappedTexSRV.p,
                                                    ImVec2 (sizeCover.x,
                                                            sizeCover.y),
                                  cover.light_info.isHDR ? hdr_uv : cover.uv0, // Top Left coordinates
                                  cover.light_info.isHDR ? hdr_uv : cover.uv1, // Bottom Right coordinates
                                  (_registry._StyleLightMode) ? ImVec4 (1.0f, 1.0f, 1.0f, fGammaCorrectedTint * AdjustAlpha (fAlpha))  :
                                                                ImVec4 (fTint, fTint, fTint, fAlpha) // Alpha transparency (2024-01-01, removed fGammaCorrectedTint * fAlpha for the light style)
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

  // Using 4.975f and 0.075f to work around some floating point shenanigans
  if (     ImGui::GetIO().MouseWheel > 0 && cover.zoom < 4.975f)
    cover.zoom += 0.05f;

  else if (ImGui::GetIO().MouseWheel < 0 && cover.zoom > 0.075f)
    cover.zoom -= 0.05f;

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
                               "Image Path:\n"
                               "Current Folder:\n"
                               "Zoom Level:");
        sprintf (szLabelsData, "%s\n"
                               "%s\n"
                               "%s\n"
                               "%.2f",  cover.file_info.filename_utf8.c_str(),
                                        cover.file_info.path_utf8.c_str(),
                                        _current_folder.path_utf8.c_str(),
                                        cover.zoom);
      }

      // Basic
      else {
        sprintf (szLabels,     "Image:\n"
                               "Zoom Level:");
        sprintf (szLabelsData, "%s\n"
                               "%3.0f %%", cover.file_info.filename_utf8.c_str(),
                                          cover.zoom * 100);
      }

      ImGui::TextUnformatted (szLabels);
      ImGui::SameLine        (posXvalues);
      ImGui::TextUnformatted (szLabelsData);

      if (_registry.bDeveloperMode)
        ImGui::TextUnformatted ("\n");
    }

    // Basic Image Details
    {
      static const char szLabels [] = "Resolution:\n"
                                      "Dynamic Range:";
      
      char     szLabelsData  [512] = { };

      sprintf (szLabelsData, "%.0fx%.0f\n"
                             "%s",    cover.width,
                                      cover.height,
                                     (cover.is_hdr) ? "HDR" : "SDR");

      ImGui::TextUnformatted (szLabels);
      ImGui::SameLine        (posXvalues);
      ImGui::TextUnformatted (szLabelsData);
    }

    // HDR Light Levels
    if (cover.light_info.isHDR && cover.pTonemappedTexSRV.p != nullptr)
    {
      ImGui::TextUnformatted ("\n");

      static const char szLightLabels [] = "MaxCLL (scRGB): \n"
                                           "Max Luminance: \n"
                                           "Min Luminance: ";
      char     szLightUnits  [512] = { };
      char     szLightLevels [512] = { };
      
      sprintf (szLightUnits, (const char*)
                           u8"(%c)\n"
                           u8"cd / m\u00b2\n" // Unicode: Superscript Two
                           u8"cd / m\u00b2", cover.light_info.max_cll_name);
      sprintf (szLightLevels, "%.3f \n"
                              "%.2f \n"
                              "%.2f ",  cover.light_info.max_cll,
                                        cover.light_info.max_nits,
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
                             ImVec2 (512.0f, 512.0f),
                             cover.uv0, // Top Left coordinates
                             cover.uv1, // Bottom Right coordinates
                             ImVec4 (1.0f, 1.0f, 1.0f, 1.0f)
      );

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

  // Act on all right clicks, because why not? :D
  if (! SKIF_ImGui_IsAnyPopupOpen ( ) && ImGui::IsMouseClicked (ImGuiMouseButton_Right))
    ContextMenu = PopupState_Open;

  // Open the Empty Space Menu
  if (ContextMenu == PopupState_Open)
    ImGui::OpenPopup    ("ContextMenu");

  if (ImGui::BeginPopup   ("ContextMenu", ImGuiWindowFlags_NoMove))
  {
    ContextMenu = PopupState_Opened;
    constexpr char spaces[] = { "\u0020\u0020\u0020\u0020" };

    ImGui::PushStyleColor (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));

    if (SKIF_ImGui_MenuItemEx2 ("Open", ICON_FA_EYE))
      OpenFileDialog = PopupState_Open;

    if (tryingToLoadImage)
    {
      ImGui::PushStyleColor  (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
      SKIF_ImGui_MenuItemEx2 ("Loading...", ICON_FA_SPINNER, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::PopStyleColor   ( );
    }

    else if (cover.pRawTexSRV.p != nullptr)
    {
      if (SKIF_ImGui_MenuItemEx2 ("Close", 0, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info)))
        _SwapOutCover ();

      // Image scaling

      ImGui::Separator ( );

      ImGui::PushID ("#ImageScaling");

      if (SKIF_ImGui_BeginMenuEx2 ("Scaling", ICON_FA_PANORAMA))
      {
        static bool bNone    = (_registry.iImageScaling == 0) ? true : false;
        static bool bFill    = (_registry.iImageScaling == 1) ? true : false;
        static bool bFit     = (_registry.iImageScaling == 2) ? true : false;
        static bool bStretch = (_registry.iImageScaling == 3) ? true : false;

        if (ImGui::MenuItem ("None", spaces,  &bNone))
        {
          _registry.iImageScaling = 0;

        //bNone    = false;
          bFill    = false;
          bFit     = false;
          bStretch = false;

          _registry.regKVImageScaling.putData (_registry.iImageScaling);
        }

        if (ImGui::MenuItem ("Fill",  spaces, &bFill))
        {
          _registry.iImageScaling = 1;

          bNone    = false;
        //bFill    = false;
          bFit     = false;
          bStretch = false;

          _registry.regKVImageScaling.putData (_registry.iImageScaling);
        }

        if (ImGui::MenuItem ("Fit",  spaces, &bFit))
        {
          _registry.iImageScaling = 2;

          bNone    = false;
          bFill    = false;
        //bFit     = false;
          bStretch = false;

          _registry.regKVImageScaling.putData (_registry.iImageScaling);
        }

#if _DEBUG
        if (ImGui::MenuItem ("Stretch",  spaces, &bStretch))
        {
          _registry.iImageScaling = 3;

          bNone    = false;
          bFill    = false;
          bFit     = false;
        //bStretch = false;

          _registry.regKVImageScaling.putData (_registry.iImageScaling);
        }
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
      
      if (SKIF_ImGui_MenuItemEx2 ("Details", ICON_FA_BARCODE, ImGui::GetStyleColorVec4 (ImGuiCol_Text), spaces, &_registry.bImageDetails))
        _registry.regKVImageDetails.putData (_registry.bImageDetails);

      ImGui::Separator       ( );

      if (! cover.file_info.path.empty() && SKIF_ImGui_MenuItemEx2 ("Open in File Explorer", ICON_FA_FOLDER_OPEN, ImColor(255, 207, 72)))
        SKIF_Util_FileExplorer_SelectFile (cover.file_info.path.c_str());
    }

    ImGui::Separator       ( );

    if (SKIF_ImGui_MenuItemEx2 ("Settings", ICON_FA_LIST_CHECK))
      SKIF_Tab_ChangeTo = UITab_Settings;

    ImGui::Separator ( );

    if (SKIF_ImGui_MenuItemEx2 ("Fullscreen", SKIF_ImGui_IsFullscreen () ? ICON_FA_DOWN_LEFT_AND_UP_RIGHT_TO_CENTER : ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER))
    {
      SKIF_ImGui_SetFullscreen (! SKIF_ImGui_IsFullscreen( ));
    }

    ImGui::Separator ( );

    if (SKIF_ImGui_MenuItemEx2 ("Exit", 0, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info)))
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
    new_path.clear();

    // We're going to stream the cover in asynchronously on this thread
    HANDLE hWorkerThread = (HANDLE)
    _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
    {
      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_ImageWorker");

      thread_s* _data = static_cast<thread_s*>(var);

      CoInitializeEx (nullptr, 0x0);

      PLOG_DEBUG << "SKIF_ImageWorker thread started!";

      PLOG_INFO  << "Streaming game cover asynchronously...";

      int queuePos = getTextureLoadQueuePos();
      //PLOG_VERBOSE << "queuePos = " << queuePos;
    
      LoadLibraryTexture ( _data->image );

      PLOG_VERBOSE << "_pRawTexSRV = "        << _data->image.pRawTexSRV;
      PLOG_VERBOSE << "_pTonemappedTexSRV = " << _data->image.pTonemappedTexSRV;

      int currentQueueLength = textureLoadQueueLength.load();

      if (currentQueueLength == queuePos)
      {
        PLOG_DEBUG << "Texture is live! Swapping it in.";

        cover.file_info         = _data->image.file_info;
        cover.width             = _data->image.width;
        cover.height            = _data->image.height;
        cover.zoom              = _data->image.zoom;
        cover.uv0               = _data->image.uv0;
        cover.uv1               = _data->image.uv1;
        cover.pRawTexSRV        = _data->image.pRawTexSRV;
        cover.pTonemappedTexSRV = _data->image.pTonemappedTexSRV;
        cover.pGamutCoverageSRV = _data->image.pGamutCoverageSRV;
        cover.pGamutCoverageUAV = _data->image.pGamutCoverageUAV;
        cover.light_info        = _data->image.light_info;
        cover.colorimetry       = _data->image.colorimetry;
        cover.is_hdr            = _data->image.is_hdr;

        // Parent folder (used for the directory watch)
        std::filesystem::path path    = SKIF_Util_NormalizeFullPath (cover.file_info.path);
        cover.file_info.folder_path   = path.parent_path().wstring();
        cover.file_info.filename      = path.filename()   .wstring();
        cover.file_info.filename_utf8 = SK_WideCharToUTF8 (cover.file_info.filename);

        extern ImVec2 SKIV_ResizeApp;
        SKIV_ResizeApp.x = cover.width;
        SKIV_ResizeApp.y = cover.height;

        // Indicate that we have stopped loading the cover
        imageLoading.store (false);

        // Force a refresh when the cover has been swapped in
        PostMessage (SKIF_Notify_hWnd, WM_SKIF_COVER, 0x0, 0x0);
      }

      else if (_data->image.pRawTexSRV.p != nullptr || _data->image.pTonemappedTexSRV != nullptr || _data->image.pGamutCoverageSRV != nullptr || _data->image.pGamutCoverageUAV != nullptr)
      {
        if (_data->image.pRawTexSRV.p != nullptr)
        {
          PLOG_DEBUG << "Texture is late! (" << queuePos << " vs " << currentQueueLength << ")";
          PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << _data->image.pRawTexSRV.p << " to be released";;
          SKIF_ResourcesToFree.push(_data->image.pRawTexSRV.p);
          _data->image.pRawTexSRV.p = nullptr;
        }

        if (_data->image.pTonemappedTexSRV != nullptr)
        {
          PLOG_DEBUG << "Texture is late! (" << queuePos << " vs " << currentQueueLength << ")";
          PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << _data->image.pTonemappedTexSRV.p << " to be released";;
          SKIF_ResourcesToFree.push(_data->image.pTonemappedTexSRV.p);
          _data->image.pTonemappedTexSRV.p = nullptr;
        }

        if (_data->image.pGamutCoverageSRV != nullptr)
        {
          PLOG_DEBUG << "Texture is late! (" << queuePos << " vs " << currentQueueLength << ")";
          PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << _data->image.pGamutCoverageSRV << " to be released";;
          SKIF_ResourcesToFree.push(_data->image.pGamutCoverageSRV.p);
          _data->image.pGamutCoverageSRV.p = nullptr;
        }

        if (_data->image.pGamutCoverageUAV != nullptr)
        {
          PLOG_DEBUG << "Texture is late! (" << queuePos << " vs " << currentQueueLength << ")";
          PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << _data->image.pGamutCoverageUAV << " to be released";;
          SKIF_ResourcesToFree.push(_data->image.pGamutCoverageUAV.p);
          _data->image.pGamutCoverageUAV.p = nullptr;
        }
      }

      delete _data;

      PLOG_INFO  << "Finished streaming image asynchronously...";
      PLOG_DEBUG << "SKIF_ImageWorker thread stopped!";

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

    LPWSTR pwszFilePath = NULL;
    HRESULT hr          =
      SK_FileOpenDialog (&pwszFilePath, COMDLG_FILTERSPEC{ L"Images", L"*.png;*.jpg;*.jpeg;*.webp;*.psd;*.bmp;*.jxr;*.hdr" }, 1, FOS_FILEMUSTEXIST, FOLDERID_Pictures);
          
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

  if (! tryingToLoadImage)
  {
    if (ImGui::IsKeyPressed (ImGuiKey_RightArrow))
    {
      dragDroppedFilePath = _current_folder.nextImage ( );
    }

    else if (ImGui::IsKeyPressed (ImGuiKey_LeftArrow))
    {
      dragDroppedFilePath = _current_folder.prevImage ( );
    }
  }

  if (! dragDroppedFilePath.empty())
  {
    PLOG_VERBOSE << "New drop was given: " << dragDroppedFilePath;

    // First position is a quotation mark -- we need to strip those
    if (dragDroppedFilePath.find(L"\"") == 0)
      dragDroppedFilePath = dragDroppedFilePath.substr(1, dragDroppedFilePath.find(L"\"", 1) - 1) + dragDroppedFilePath.substr(dragDroppedFilePath.find(L"\"", 1) + 1, std::wstring::npos);

    std::error_code ec;
    std::wstring targetPath = L"";
    const std::wstring ext  = std::filesystem::path(dragDroppedFilePath.data()).extension().wstring();
    bool         isURL      = PathIsURL (dragDroppedFilePath.data());
    PLOG_VERBOSE << "    File extension: " << ext;

    bool isImage = false;

    for (auto& type : supported_formats)
    {
      if (SKIF_Util_HasFileExtension (ext, type))
      {
        isImage = true;
        break;
      }
    }

    // URLs + non-images
    if (isURL || ! isImage)
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

        if (prev_ext != type_ext) // Filter out duplicates (e.g. .tiff and .gif)
          error_label += "   *" + type_ext + "\n";

        prev_ext = type_ext;
      }
      error_label   += "\n"
                       "Note that the app has no support for animated images.";

      SKIF_ImGui_InfoMessage (error_title, error_label);
    }

    else
      new_path = dragDroppedFilePath;

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

      if (cover.pTonemappedTexSRV.p != nullptr)
      {
        SKIF_ResourcesToFree.push(cover.pTonemappedTexSRV.p);
        cover.pTonemappedTexSRV.p = nullptr;
      }
    }

    if (cover_old.pRawTexSRV.p != nullptr)
    {
      SKIF_ResourcesToFree.push(cover_old.pRawTexSRV.p);
      cover_old.pRawTexSRV.p = nullptr;

      if (cover_old.pTonemappedTexSRV.p != nullptr)
      {
        SKIF_ResourcesToFree.push(cover_old.pTonemappedTexSRV.p);
        cover_old.pTonemappedTexSRV.p = nullptr;
      }

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
  
}
