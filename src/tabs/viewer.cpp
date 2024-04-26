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
  CComPtr <ID3D11ShaderResourceView> pTexSRV;

  // copy assignment
  image_s& operator= (const image_s other) noexcept
  {
    path             = other.path;
    width            = other.width;
    height           = other.height;
    uv0              = other.uv0;
    uv1              = other.uv1;
    avail_size       = other.avail_size;
    avail_size_cache = other.avail_size_cache;
    pTexSRV.p        = other.pTexSRV.p;
    return *this;
  }

  void reset (void)
  {
    path             = L"";
    width            = 0.0f;
    height           = 0.0f;
    uv0              = ImVec2 (0, 0);
    uv1              = ImVec2 (1, 1);
    avail_size       = { };
    avail_size_cache = { };
    pTexSRV.p        = nullptr;
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
  ImageDecoder_WIC,
  ImageDecoder_stbi
};

void
LoadLibraryTexture (image_s& image)
{
  // NOT REALLY THREAD-SAFE WHILE IT RELIES ON THESE STATIC GLOBAL OBJECTS!
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  CComPtr <ID3D11Texture2D> pTex2D;
  DirectX::TexMetadata        meta = { };
  DirectX::ScratchImage        img = { };

  bool succeeded    = false;

  DWORD pre = SKIF_Util_timeGetTime1();

  if (image.path.empty())
    return;

  const std::filesystem::path imagePath (image.path.data());
  std::wstring ext = SKIF_Util_ToLowerW (imagePath.extension().wstring());
  std::string szPath = SK_WideCharToUTF8(image.path);

  ImageDecoder decoder = 
    (ext == L".jpeg") ? ImageDecoder_stbi :
    (ext == L".jpg" ) ? ImageDecoder_stbi :
    (ext == L".png" ) ? ImageDecoder_stbi :
    (ext == L".tga" ) ? ImageDecoder_stbi :
    (ext == L".bmp" ) ? ImageDecoder_stbi :
    (ext == L".psd" ) ? ImageDecoder_stbi :
    (ext == L".gif" ) ? ImageDecoder_stbi :
    (ext == L".hdr" ) ? ImageDecoder_stbi :
                        ImageDecoder_WIC; // Use WIC as a generic fallback for all other files (.jxr, .webp, .tif)

  PLOG_DEBUG_IF(decoder == ImageDecoder_stbi) << "Using stbi decoder...";
  PLOG_DEBUG_IF(decoder == ImageDecoder_WIC ) << "Using WIC decoder...";

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
            DirectX::WIC_FLAGS_FILTER_POINT | DirectX::WIC_FLAGS_IGNORE_SRGB, // WIC_FLAGS_IGNORE_SRGB solves some PNGs appearing too dark
              &meta, img)))
    {
      succeeded = true;
    }
  }

  // Push the existing texture to a stack to be released after the frame
  //   Do this regardless of whether we could actually load the new cover or not
  if (image.pTexSRV.p != nullptr)
  {
    extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;
    PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << image.pTexSRV.p << " to be released";;
    SKIF_ResourcesToFree.push (image.pTexSRV.p);
    image.pTexSRV.p = nullptr;
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

  pTex2D = nullptr;

  if (SUCCEEDED (DirectX::CreateTexture (pDevice, pImg->GetImages(), pImg->GetImageCount(), meta, (ID3D11Resource **)&pTex2D.p)))
  {
    D3D11_SHADER_RESOURCE_VIEW_DESC
    srv_desc                           = { };
    srv_desc.Format                    = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels       = UINT_MAX;
    srv_desc.Texture2D.MostDetailedMip =  0;

    if (pTex2D.p != nullptr && SUCCEEDED (pDevice->CreateShaderResourceView (pTex2D.p, &srv_desc, &image.pTexSRV.p)))
    {
      DWORD post = SKIF_Util_timeGetTime1 ( );
      PLOG_INFO << "[Image Processing] Processed image in " << (post - pre) << " ms.";

      // Update the image width/height
      image.width  = static_cast<float>(meta.width);
      image.height = static_cast<float>(meta.height);
    }

    // SRV is holding a reference, this is not needed anymore.
    pTex2D = nullptr;
  }
};

#pragma endregion

#pragma region AspectRatio

ImVec2
GetCurrentAspectRatio (image_s& image)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static int last_scaling = 0;

  ImVec2 avail_size = ImGui::GetContentRegionAvail();

  // Clear any cached data on image changes
  if (image.pTexSRV.p == nullptr || image.height == 0 || image.width  == 0)
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
    if (cover.pTexSRV.p != nullptr)
    {
      // If there already is an old cover, we need to push it for release
      if (cover_old.pTexSRV.p != nullptr)
      {
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << cover_old.pTexSRV.p << " to be released";;
        SKIF_ResourcesToFree.push(cover_old.pTexSRV.p);
        cover_old.pTexSRV.p = nullptr;
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
    if (cover_old.pTexSRV.p != nullptr)
    {
      PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << cover_old.pTexSRV.p << " to be released";;
      SKIF_ResourcesToFree.push(cover_old.pTexSRV.p);
      cover_old.pTexSRV.p = nullptr;
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

  ImVec2 sizeCover     = GetCurrentAspectRatio (cover);
  ImVec2 sizeCover_old = GetCurrentAspectRatio (cover_old);

  // From now on ImGui UI calls starts being made...

#pragma region GameCover
  
  static int    queuePosGameCover  = 0;
  static char   cstrLabelLoading[] = "...";
  static char   cstrLabelMissing[] = "Drop an image...";
  bool          isImageHovered = false;

  ImVec2 originalPos    = ImGui::GetCursorPos ( );
         originalPos.x -= 1.0f * SKIF_ImGui_GlobalDPIScale;

  if (loadImage)
  {
    // A new cover is meant to be loaded, so don't do anything for now...
  }

  else if (tryingToLoadImage)
  {
    ImGui::SetCursorPos (ImVec2 (
      originalPos.x + (sizeCover.x / 2) * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelLoading).x / 2,
      originalPos.y + (sizeCover.y / 2) * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelLoading).y / 2));
    ImGui::TextDisabled (  cstrLabelLoading);
  }
  
  else if (textureLoadQueueLength.load() == queuePosGameCover && cover.pTexSRV.p == nullptr)
  {
    ImGui::SetCursorPos (ImVec2 (
      originalPos.x + (sizeCover.x / 2) * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelMissing).x / 2,
      originalPos.y + (sizeCover.y / 2) * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelMissing).y / 2));
    ImGui::TextDisabled (  cstrLabelMissing);
  }

  else if (cover    .pTexSRV.p == nullptr &&
           cover_old.pTexSRV.p == nullptr)
  {
    ImGui::SetCursorPos (ImVec2 (
      originalPos.x + (sizeCover.x / 2) * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelMissing).x / 2,
      originalPos.y + (sizeCover.y / 2) * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelMissing).y / 2));
    ImGui::TextDisabled (  cstrLabelMissing);
  }

  ImGui::SetCursorPos (originalPos);

  float fGammaCorrectedTint = 
    ((! _registry._RendererHDREnabled && _registry.iSDRMode == 2) || 
      (  _registry._RendererHDREnabled && _registry.iHDRMode == 2))
        ? AdjustAlpha (fTint)
        : fTint;

  // Display previous fading out cover
  if (cover_old.pTexSRV.p != nullptr && fAlphaPrev > 0.0f)
  {
    if (sizeCover_old.x < ImGui::GetContentRegionAvail().x)
      ImGui::SetCursorPosX ((ImGui::GetContentRegionAvail().x - sizeCover_old.x * SKIF_ImGui_GlobalDPIScale) * 0.5f);
    if (sizeCover_old.y < ImGui::GetContentRegionAvail().y)
      ImGui::SetCursorPosY ((ImGui::GetContentRegionAvail().y - sizeCover_old.y * SKIF_ImGui_GlobalDPIScale) * 0.5f);

    SKIF_ImGui_OptImage  (cover_old.pTexSRV.p,
                                                      ImVec2 (sizeCover_old.x * SKIF_ImGui_GlobalDPIScale,
                                                              sizeCover_old.y * SKIF_ImGui_GlobalDPIScale),
                                                      cover_old.uv0, // Top Left coordinates
                                                      cover_old.uv1, // Bottom Right coordinates
                                    (_registry._StyleLightMode) ? ImVec4 (1.0f, 1.0f, 1.0f, fGammaCorrectedTint * AdjustAlpha (fAlphaPrev))  : ImVec4 (fTint, fTint, fTint, fAlphaPrev) // Alpha transparency
    );

    ImGui::SetCursorPos (originalPos);
  }

  if (sizeCover.x < ImGui::GetContentRegionAvail().x)
    ImGui::SetCursorPosX ((ImGui::GetContentRegionAvail().x - sizeCover.x * SKIF_ImGui_GlobalDPIScale) * 0.5f);
  if (sizeCover.y < ImGui::GetContentRegionAvail().y)
    ImGui::SetCursorPosY ((ImGui::GetContentRegionAvail().y - sizeCover.y * SKIF_ImGui_GlobalDPIScale) * 0.5f);

  // Display game cover image
  SKIF_ImGui_OptImage  (cover.pTexSRV.p,
                                                    ImVec2 (sizeCover.x * SKIF_ImGui_GlobalDPIScale,
                                                            sizeCover.y * SKIF_ImGui_GlobalDPIScale),
                                                    cover.uv0, // Top Left coordinates
                                                    cover.uv1, // Bottom Right coordinates
                                  (_registry._StyleLightMode) ? ImVec4 (1.0f, 1.0f, 1.0f, fGammaCorrectedTint * AdjustAlpha (fAlpha))  : ImVec4 (fTint, fTint, fTint, fAlpha) // Alpha transparency (2024-01-01, removed fGammaCorrectedTint * fAlpha for the light style)
  );

  isImageHovered = ImGui::IsItemHovered();

#pragma endregion

  if (! SKIF_ImGui_IsAnyPopupOpen() &&
    ImGui::IsItemClicked (ImGuiMouseButton_Right))
    ContextMenu = PopupState_Open;

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
        SK_FileOpenDialog (&pwszFilePath, COMDLG_FILTERSPEC{ L"Images", L"*.png;*.jpg;*.jpeg;*.webp;*.psd;*.bmp" }, 1, FOS_FILEMUSTEXIST, FOLDERID_Pictures);
          
      if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
      {
        // If cancelled, do nothing
      }

      else if (SUCCEEDED(hr))
      {
        dragDroppedFilePath = pwszFilePath;
      }
    }

    if (cover.pTexSRV.p != nullptr)
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

      PLOG_VERBOSE << "_pTexSRV = " << _data->image.pTexSRV;

      int currentQueueLength = textureLoadQueueLength.load();

      if (currentQueueLength == queuePos)
      {
        PLOG_DEBUG << "Texture is live! Swapping it in.";

        cover.width   = _data->image.width;
        cover.height  = _data->image.height;
        cover.uv0     = _data->image.uv0;
        cover.uv1     = _data->image.uv1;
        cover.pTexSRV = _data->image.pTexSRV;

        extern ImVec2 SKIV_ResizeApp;
        SKIV_ResizeApp.x = cover.width;
        SKIV_ResizeApp.y = cover.height;

        // Indicate that we have stopped loading the cover
        imageLoading.store (false);

        // Force a refresh when the cover has been swapped in
        PostMessage (SKIF_Notify_hWnd, WM_SKIF_COVER, 0x0, 0x0);
      }

      else if (_data->image.pTexSRV.p != nullptr)
      {
        PLOG_DEBUG << "Texture is late! (" << queuePos << " vs " << currentQueueLength << ")";
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << _data->image.pTexSRV.p << " to be released";;
        SKIF_ResourcesToFree.push(_data->image.pTexSRV.p);
        _data->image.pTexSRV.p = nullptr;
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
    if (fAlpha < 1.0f && cover.pTexSRV.p != nullptr)
    {
      if (current_time - timeLastTick > 15)
      {
        fAlpha += 0.05f;
        incTick = true;
      }

      imageFadeActive = true;
    }

    // Fade out the old one
    if (fAlphaPrev > 0.0f && cover_old.pTexSRV.p != nullptr)
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
  if (resetScrollCenter)
  {
    resetScrollCenter = false;

    ImGui::SetScrollHereX ( );
    ImGui::SetScrollHereY ( );
  }

  // In case of a device reset, unload all currently loaded textures
  if (invalidatedDevice == 1)
  {   invalidatedDevice  = 2;

    if (cover.pTexSRV.p != nullptr)
    {
      SKIF_ResourcesToFree.push(cover.pTexSRV.p);
      cover.pTexSRV.p = nullptr;
    }

    if (cover_old.pTexSRV.p != nullptr)
    {
      SKIF_ResourcesToFree.push(cover_old.pTexSRV.p);
      cover_old.pTexSRV.p = nullptr;
    }

    // Trigger a refresh of the cover
    loadImage = true;
  }
  
}
