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

bool                   loadImage         = false;
bool                   tryingToLoadImage = false;
std::atomic<bool>      imageLoading      = false;
bool                   resetScrollCenter = false;

bool                   coverRefresh      = false; // This just triggers a refresh of the cover
std::wstring           coverRefreshPath  = L"";
int                    coverRefreshCount = 0;
int                    numRegular        = 0;
int                    numPinnedOnTop    = 0;

const float fTintMin     = 0.75f;
      float fTint        = 1.0f;
      float fAlpha       = 0.0f;
      float fAlphaPrev   = 1.0f;
      
PopupState ContextMenu     = PopupState_Closed;

struct image_s {
  std::wstring path        = L"";
  float        width       = 0.0f;
  float        height      = 0.0f;
  ImVec2       uv0 = ImVec2 (0, 0);
  ImVec2       uv1 = ImVec2 (1, 1);
  ImVec2       avail_size;        // Holds the frame size used (affected by the scaling method)
  ImVec2       avail_size_cache;  // Holds a cached value used to determine if avail_size needs recalculating
  CComPtr <ID3D11ShaderResourceView> pRawTexSRV;
  CComPtr <ID3D11ShaderResourceView> pTonemappedTexSRV;

  struct light_info_s {
    float max_cll      = 0.0f;
    char  max_cll_name =  '?';
    float max_nits     = 0.0f;
    float min_nits     = 0.0f;
    float avg_nits     = 0.0f;
  } light_info;

  // copy assignment
  image_s& operator= (const image_s other) noexcept
  {
    path                = other.path;
    width               = other.width;
    height              = other.height;
    uv0                 = other.uv0;
    uv1                 = other.uv1;
    avail_size          = other.avail_size;
    avail_size_cache    = other.avail_size_cache;
    pRawTexSRV.p        = other.pRawTexSRV.p;
    pTonemappedTexSRV.p = other.pTonemappedTexSRV.p;
    light_info          = other.light_info;
    return *this;
  }

  void reset (void)
  {
    path                = L"";
    width               = 0.0f;
    height              = 0.0f;
    uv0                 = ImVec2 (0, 0);
    uv1                 = ImVec2 (1, 1);
    avail_size          = { };
    avail_size_cache    = { };
    pRawTexSRV.p        = nullptr;
    pTonemappedTexSRV.p = nullptr;
    light_info          = { };
  }
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
  // NOT REALLY THREAD-SAFE WHILE IT RELIES ON THESE STATIC GLOBAL OBJECTS!
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  CComPtr <ID3D11Texture2D> pRawTex2D;
  CComPtr <ID3D11Texture2D> pTonemappedTex2D;
  DirectX::TexMetadata        meta = { };
  DirectX::ScratchImage        img = { };

  bool succeeded    = false;

  DWORD pre = SKIF_Util_timeGetTime1();

  if (image.path.empty())
    return;

  const std::filesystem::path imagePath (image.path.data());
  std::wstring ext = SKIF_Util_ToLowerW (imagePath.extension().wstring());
  std::string szPath = SK_WideCharToUTF8(image.path);

  ImageDecoder decoder = ImageDecoder_None;

  // Identify file type by reading the file signature
  static const auto types =
  {
    FileSignature { L"image/jpeg",                L".jpg",  { 0xFF, 0xD8, 0x00, 0x00 },   // JPEG (SOI; Start of Image)
                                                            { 0xFF, 0xFF, 0x00, 0x00 } }, // JPEG App Markers are masked as they can be all over the place (e.g. 0xFF 0xD8 0xFF 0xED)
    FileSignature { L"image/png",                 L".png",  { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A } },
    FileSignature { L"image/webp",                L".webp", { 0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50 },     // 52 49 46 46 ?? ?? ?? ?? 57 45 42 50
                                                            { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF } }, // mask
    FileSignature { L"image/bmp",                 L".bmp",  { 0x42, 0x4D } },
    FileSignature { L"image/vnd.ms-photo",        L".jxr",  { 0x49, 0x49, 0xBC } },
    FileSignature { L"image/vnd.adobe.photoshop", L".psd",  { 0x38, 0x42, 0x50, 0x53 } },
    FileSignature { L"image/tiff",                L".tiff", { 0x49, 0x49, 0x2A, 0x00 } }, // TIFF: little-endian
    FileSignature { L"image/tiff",                L".tiff", { 0x4D, 0x4D, 0x00, 0x2A } }, // TIFF: big-endian
    FileSignature { L"image/vnd.radiance",        L".hdr",  { 0x23, 0x3F, 0x52, 0x41, 0x44, 0x49, 0x41, 0x4E, 0x43, 0x45, 0x0A } }, // Radiance High Dynamic Range image file
    FileSignature { L"image/gif",                 L".gif",  { 0x47, 0x49, 0x46, 0x38, 0x37, 0x61 } }, // GIF87a
    FileSignature { L"image/gif",                 L".gif",  { 0x47, 0x49, 0x46, 0x38, 0x39, 0x61 } }  // GIF89a
  //FileSignature { L"image/x-targa",             L".tga",  { 0x00, } }, // TGA has no real unique header identifier, so just use the file extension on those
  };

  if (ext == L".tga")
    decoder = ImageDecoder_stbi;

  if (decoder == ImageDecoder_None)
  {
    static size_t
        maxLength  = 0;
    if (maxLength == 0)
    {
      for (auto& type : types)
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

      for (auto& type : types)
      {
        if (SKIF_Util_HasFileSignature (buffer, type))
        {
          PLOG_INFO << "Detected an " << type.mime_type << " image";

          decoder = 
            (type.file_extension == L".jpg"  ) ? ImageDecoder_stbi : // covers both .jpeg and .jpg
            (type.file_extension == L".png"  ) ? ImageDecoder_WIC :  // Use WIC for proper color correction
          //(type.file_extension == L".tga"  ) ? ImageDecoder_stbi : // TGA has no real unique header identifier, so just use the file extension on those
            (type.file_extension == L".bmp"  ) ? ImageDecoder_stbi :
            (type.file_extension == L".psd"  ) ? ImageDecoder_stbi :
            (type.file_extension == L".gif"  ) ? ImageDecoder_stbi :
            (type.file_extension == L".hdr"  ) ? ImageDecoder_stbi :
            (type.file_extension == L".jxr"  ) ? ImageDecoder_WIC  :
            (type.file_extension == L".webp" ) ? ImageDecoder_WIC  :
            (type.file_extension == L".tiff" ) ? ImageDecoder_WIC  :
                                                 ImageDecoder_WIC;   // Not actually being used

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

    unsigned char *pixels = stbi_load (szPath.c_str(), &width, &height, &channels_in_file, desired_channels);

    if (pixels != NULL)
    {
      meta.width     = width;
      meta.height    = height;
      meta.depth     = 1;
      meta.arraySize = 1;
      meta.mipLevels = 1;
      meta.format    = DXGI_FORMAT_R8G8B8A8_UNORM; // STBI_rgb_alpha
      meta.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

      if (SUCCEEDED (img.Initialize2D (meta.format, width, height, 1, 1)))
      {
        size_t   imageSize = width * height * desired_channels * sizeof(uint8_t);
        uint8_t* pDest     = img.GetImage(0, 0, 0)->pixels;
        memcpy(pDest, pixels, imageSize);

        succeeded = true;
      }
    }

    stbi_image_free (pixels);
  }

  else if (decoder == ImageDecoder_WIC)
  {
    if (SUCCEEDED (
        DirectX::LoadFromWICFile (
          image.path.c_str (),
            DirectX::WIC_FLAGS_FILTER_POINT,
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

  if (meta.format == DXGI_FORMAT_R16G16B16A16_FLOAT)
  {
using namespace DirectX;

    static const XMVECTORF32 s_luminance_2020 =
      { 0.2627f,   0.678f,    0.0593f,   0.f };

    static const XMMATRIX c_from2020to709 =
    {
      {  1.6604910f, -0.1245505f, -0.0181508f, 0.f },
      { -0.5876411f,  1.1328999f, -0.1005789f, 0.f },
      { -0.0728499f, -0.0083494f,  1.1187297f, 0.f },
      {  0.f,         0.f,         0.f,        1.f }
    };

    static const XMMATRIX c_from709to2020 =
    {
      { 0.627225305694944f,  0.329476882715808f,  0.0432978115892484f, 0.0f },
      { 0.0690418812810714f, 0.919605681354755f,  0.0113524373641739f, 0.0f },
      { 0.0163911702607078f, 0.0880887513437058f, 0.895520078395586f,  0.0f },
      { 0.0f,                0.0f,                0.0f,                1.0f }
    };

    XMVECTOR vMaxCLL = g_XMZero;
    float    fMaxLum = 0.0f;
    float    fMinLum = FLT_MAX;

    EvaluateImage ( pImg->GetImages     (),
                    pImg->GetImageCount (),
                    pImg->GetMetadata   (),
    [&](const XMVECTOR* pixels, size_t width, size_t y)
    {
      UNREFERENCED_PARAMETER(y);

      for (size_t j = 0; j < width; ++j)
      {
        XMVECTOR v = *pixels;

        vMaxCLL =
          XMVectorMax (v, vMaxCLL);

        v = XMVector3Transform (v, c_from709to2020);

        XMVECTOR vLum =
          XMVector3Dot ( v, s_luminance_2020 );

        fMaxLum =
          std::max (fMaxLum, vLum.m128_f32 [0]);

        fMinLum =
          std::min (fMinLum, vLum.m128_f32 [0]);

        //lumTotal +=
        //  logf ( std::max (0.000001f, 0.000001f + v.m128_f32 [1]) ),
        //++N;

        pixels++;
      }
    } );

    const float fMaxCLL =
      std::max ({
        vMaxCLL.m128_f32 [0],
        vMaxCLL.m128_f32 [1],
        vMaxCLL.m128_f32 [2]
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
      fMaxCLL == vMaxCLL.m128_f32 [0] ? 'R' :
      fMaxCLL == vMaxCLL.m128_f32 [1] ? 'G' :
      fMaxCLL == vMaxCLL.m128_f32 [2] ? 'B' :
                                        'X';

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
    if (meta.format == DXGI_FORMAT_R16G16B16A16_FLOAT)
      DirectX::CreateTexture (pDevice, normalized_hdr.GetImages (), normalized_hdr.GetImageCount (), normalized_hdr.GetMetadata (), (ID3D11Resource **)&pTonemappedTex2D.p);

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
    pRawTex2D        = nullptr;
    pTonemappedTex2D = nullptr;
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

      // Set up the current one to be released
      cover_old = cover;
      cover.reset();

      fAlphaPrev          = (_registry.bFadeCovers) ? fAlpha   : 0.0f;
      fAlpha              = (_registry.bFadeCovers) ?   0.0f   : 1.0f;
    }
  };

#pragma region Initialization

  SK_RunOnce (fAlpha = (_registry.bFadeCovers) ? 0.0f : 1.0f);

  DWORD       current_time = SKIF_Util_timeGetTime ( );

  // Load a new cover
  // Ensure we aren't already loading this cover
  if (! new_path.empty() &&
        new_path != cover.path)
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

#pragma endregion

  ImVec2 sizeCover     = GetCurrentAspectRatio (cover)     * ((_registry.iImageScaling == 0) ? 1 : SKIF_ImGui_GlobalDPIScale);
  ImVec2 sizeCover_old = GetCurrentAspectRatio (cover_old) * ((_registry.iImageScaling == 0) ? 1 : SKIF_ImGui_GlobalDPIScale);

  // From now on ImGui UI calls starts being made...

#pragma region GameCover
  
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
      ImGui::GetContentRegionAvail ( ).x / 2 - labelSize.x / 2,
      ImGui::GetContentRegionAvail ( ).y / 2 - labelSize.y / 2));
    ImGui::TextDisabled (pcstrLabel);
  }

  ImGui::SetCursorPos (originalPos);

  float fGammaCorrectedTint = 
    ((! _registry._RendererHDREnabled && _registry.iSDRMode == 2) ||
      ( _registry._RendererHDREnabled && _registry.iHDRMode == 2))
        ? AdjustAlpha (fTint)
        : fTint;

  D3D11_TEXTURE2D_DESC texDesc = { };

  if (cover_old.pRawTexSRV != nullptr)
  {
    CComPtr <ID3D11Resource>         pRes;
    cover_old.pRawTexSRV->GetResource (&pRes.p);

    if (pRes != nullptr)
    {
      if (CComQIPtr <ID3D11Texture2D> pTex2D (pRes);
                                      pTex2D != nullptr)
      {
        pTex2D->GetDesc (&texDesc);
      }
    }
  }
  
  bool bIsHDR =
    texDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;

  static const ImVec2 hdr_uv (-2048.0f, -2048.0f);

  // Display previous fading out cover
  if (cover_old.pRawTexSRV.p != nullptr && fAlphaPrev > 0.0f)
  {
    if (sizeCover_old.x < ImGui::GetContentRegionAvail().x)
      ImGui::SetCursorPosX ((ImGui::GetContentRegionAvail().x - sizeCover_old.x) * 0.5f);
    if (sizeCover_old.y < ImGui::GetContentRegionAvail().y)
      ImGui::SetCursorPosY ((ImGui::GetContentRegionAvail().y - sizeCover_old.y) * 0.5f);
  
    SKIF_ImGui_OptImage  ((_registry._RendererHDREnabled || (! bIsHDR)) ? cover_old.pRawTexSRV.p :
                                                                          cover_old.pTonemappedTexSRV,
                                                      ImVec2 (sizeCover_old.x,
                                                              sizeCover_old.y),
                                    bIsHDR ? hdr_uv : cover_old.uv0, // Top Left coordinates
                                    bIsHDR ? hdr_uv : cover_old.uv1, // Bottom Right coordinates
                                    (_registry._StyleLightMode) ? ImVec4 (1.0f, 1.0f, 1.0f, fGammaCorrectedTint * AdjustAlpha (fAlphaPrev))  : ImVec4 (fTint, fTint, fTint, fAlphaPrev) // Alpha transparency
    );
  
    ImGui::SetCursorPos (originalPos);
  }

  if (cover.pRawTexSRV != nullptr)
  {
    CComPtr <ID3D11Resource>     pRes;
    cover.pRawTexSRV->GetResource (&pRes.p);

    if (pRes != nullptr)
    {
      if (CComQIPtr <ID3D11Texture2D> pTex2D (pRes);
                                      pTex2D != nullptr)
      {
        pTex2D->GetDesc (&texDesc);
      }
    }
  }

  bIsHDR =
    texDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;

  if (sizeCover.x < ImGui::GetContentRegionAvail().x)
    ImGui::SetCursorPosX ((ImGui::GetContentRegionAvail().x - sizeCover.x) * 0.5f);
  if (sizeCover.y < ImGui::GetContentRegionAvail().y)
    ImGui::SetCursorPosY ((ImGui::GetContentRegionAvail().y - sizeCover.y) * 0.5f);

  // Display game cover image
  SKIF_ImGui_OptImage  ((_registry._RendererHDREnabled || (! bIsHDR)) ? cover.pRawTexSRV.p :
                                                                        cover.pTonemappedTexSRV.p,
                                                    ImVec2 (sizeCover.x,
                                                            sizeCover.y),
                                  bIsHDR ? hdr_uv : cover.uv0, // Top Left coordinates
                                  bIsHDR ? hdr_uv : cover.uv1, // Bottom Right coordinates
                                  (_registry._StyleLightMode) ? ImVec4 (1.0f, 1.0f, 1.0f, fGammaCorrectedTint * AdjustAlpha (fAlpha))  :
                                                                ImVec4 (fTint, fTint, fTint, fAlpha) // Alpha transparency (2024-01-01, removed fGammaCorrectedTint * fAlpha for the light style)
  );

  isImageHovered = ImGui::IsItemHovered();

#pragma endregion

  if (! SKIF_ImGui_IsAnyPopupOpen() &&
    ImGui::IsItemClicked (ImGuiMouseButton_Right))
    ContextMenu = PopupState_Open;

  // HDR Light Levels
  if (bIsHDR && cover.pTonemappedTexSRV.p != nullptr)
  {
    static const char szLightLabels [] = "MaxCLL:\t\t\n"
                                         "Max Luminance:\t\n"
                                         "Min Luminance:\t";

    char     szLightUnits  [512] = { };
    char     szLightLevels [512] = { };
    sprintf (szLightUnits, "(%c)\n"
                           "nits\n"
                           "nits", cover.light_info.max_cll_name);

    sprintf (szLightLevels, "%8.3f \n"
                            "%7.2f \n"
                            "%7.2f ", cover.light_info.max_cll,
                                      cover.light_info.max_nits,
                                      cover.light_info.min_nits);

    auto orig_pos =
      ImGui::GetCursorPos ();

    ImGui::SetCursorPos    (ImVec2 (0.0f, 0.0f));
    ImGui::TextUnformatted (szLightLabels);
    ImGui::SameLine        ();
    ImGui::TextUnformatted (szLightLevels);
    ImGui::SameLine        ();
    ImGui::TextUnformatted (szLightUnits);
    ImGui::SetCursorPos    (orig_pos);
  }

#pragma region ContextMenu

  // Open the Empty Space Menu
  if (ContextMenu == PopupState_Open)
    ImGui::OpenPopup    ("ContextMenu");

  if (ImGui::BeginPopup   ("ContextMenu", ImGuiWindowFlags_NoMove))
  {
    ContextMenu = PopupState_Opened;
    constexpr char spaces[] = { "\u0020\u0020\u0020\u0020" };

    ImGui::PushStyleColor (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));

    if (SKIF_ImGui_MenuItemEx2 ("Open", ICON_FA_FOLDER_OPEN, ImColor(255, 207, 72)))
    {
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
    }

    if (cover.pRawTexSRV.p != nullptr)
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

        if (ImGui::MenuItem ("Stretch",  spaces, &bStretch))
        {
          _registry.iImageScaling = 3;

          bNone    = false;
          bFill    = false;
          bFit     = false;
        //bStretch = false;

          _registry.regKVImageScaling.putData (_registry.iImageScaling);
        }

        ImGui::EndMenu ( );
      }

      ImGui::PopID ( );
    }

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

    struct thread_s {
      image_s image = { };
    };
  
    thread_s* data = new thread_s;

    data->image.path = new_path;
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

        cover.width             = _data->image.width;
        cover.height            = _data->image.height;
        cover.uv0               = _data->image.uv0;
        cover.uv1               = _data->image.uv1;
        cover.pRawTexSRV        = _data->image.pRawTexSRV;
        cover.pTonemappedTexSRV = _data->image.pTonemappedTexSRV;
        cover.light_info        = _data->image.light_info;

        extern ImVec2 SKIV_ResizeApp;
        SKIV_ResizeApp.x = cover.width;
        SKIV_ResizeApp.y = cover.height;

        // Indicate that we have stopped loading the cover
        imageLoading.store (false);

        // Force a refresh when the cover has been swapped in
        PostMessage (SKIF_Notify_hWnd, WM_SKIF_COVER, 0x0, 0x0);
      }

      else if (_data->image.pRawTexSRV.p != nullptr || _data->image.pTonemappedTexSRV != nullptr)
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

  if (! dragDroppedFilePath.empty())
  {
    PLOG_VERBOSE << "New drop was given: " << dragDroppedFilePath;

    // First position is a quotation mark -- we need to strip those
    if (dragDroppedFilePath.find(L"\"") == 0)
      dragDroppedFilePath = dragDroppedFilePath.substr(1, dragDroppedFilePath.find(L"\"", 1) - 1) + dragDroppedFilePath.substr(dragDroppedFilePath.find(L"\"", 1) + 1, std::wstring::npos);

    std::error_code ec;
    const std::filesystem::path fsPath (dragDroppedFilePath.data());
    std::wstring targetPath = L"";
    std::wstring ext        = SKIF_Util_ToLowerW  (fsPath.extension().wstring());
    bool         isURL      = PathIsURL (dragDroppedFilePath.data());
    PLOG_VERBOSE << "    File extension: " << ext;

    bool isImage =
      (ext == L".jpg"  ||
       ext == L".jpeg" ||
       ext == L".jxr"  ||
       ext == L".png"  ||
       ext == L".webp" ||
       ext == L".tga"  ||
       ext == L".bmp"  ||
       ext == L".psd"  ||
       ext == L".gif"  ||
       ext == L".tif"  ||
       ext == L".tiff" ||
       ext == L".hdr"  ); // Radiance RGBE (.hdr)

    // URLs + non-images
    if (isURL || ! isImage)
    {
      constexpr char* error_title =
        "Unsupported file format";
      constexpr char* error_label =
        "Use one of the following supported formats:\n"
        "   *.jpg\n"
        "   *.jxr\n"
        "   *.png\n"
        "   *.webp\n"
        "   *.tga\n"
        "   *.bmp\n"
        "   *.psd\n"
        "   *.gif\n"
        "   *.tif\n"
        "\n"
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

  // Reset scroll (center-align the scroll)
  if (resetScrollCenter && cover_old.pRawTexSRV.p == nullptr)
  {
    resetScrollCenter = false;

    ImGui::SetScrollHereX ( );
    ImGui::SetScrollHereY ( );
  }

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
    }

    // Trigger a refresh of the cover
    loadImage = true;
  }
  
}
