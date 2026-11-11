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

#include <functional>

// Decoding images (and image_s): utility/image_decode.cpp; loading them and releasing their textures: utility/image_store.cpp
#include <utility/image_decode.h>
#include <utility/image_store.h>

#include <thread>

#include <html_coder.hpp>
#include <utility/image.h>

#pragma comment (lib, "dxguid.lib")

ImRect copyRect = { 0,0,0,0 };
bool wantCopyToClipboard = false;


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


// How images are shown (the same for every image): their scaling, and the zoom on top of it (1.0f = 100%; 0.05f to 5.0f)
static struct {
  float        zoom    = 1.0f;
  ImageScaling scaling = ImageScaling_Auto;
} s_view;

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

extern CComPtr <ID3D11Device> SKIF_D3D11_GetDevice (bool bWait = true);
extern bool            allowShortcutCtrlA;
extern ImVec2          SKIF_vecAlteredSize;
extern float           SKIF_ImGui_GlobalDPIScale;
extern std::string     SKIF_StatusBarHelp;
extern std::string     SKIF_StatusBarText;
extern DWORD           invalidatedDevice;
extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;

// Animated images, for the main loop (SKIV.cpp): when the frame shown changes, the window is presented again even though
//   nothing else changed, and while one plays, the loop wakes up when its next frame is due.
static DWORD SKIV_Viewer_AnimNextFrameAt = 0;     // When the next frame is due (SKIF_Util_timeGetTime1); 0: none
static int   SKIV_Viewer_AnimDrawnFrame  = -1;    // The last frame (ImGui::GetFrameCount) an animation was drawn in
static bool  SKIV_Viewer_AnimChanged     = false; // The frame shown changed since the window was last presented

// The thumbnails carousel, for the main loop: when it is due to start fading out (once the mouse has left it); 0: none
static DWORD SKIV_Viewer_CarouselWakeAt  = 0;

// ms until the next frame of the animation shown is due (0: now), or the thumbnails carousel is due to fade out;
//   INFINITE if neither
DWORD
SKIV_Viewer_GetAnimationWakeIn (void)
{
  if (SKIF_ImGui_hWnd == NULL || ::IsIconic (SKIF_ImGui_hWnd))
    return INFINITE;

  auto _WakeIn = [](DWORD at) -> DWORD {
    const LONG wakeIn = static_cast <LONG> (at - SKIF_Util_timeGetTime1 ( ));
    return (wakeIn > 0) ? static_cast <DWORD> (wakeIn) : 0;
  };

  DWORD wakeIn = INFINITE;

  if (SKIV_Viewer_AnimNextFrameAt != 0 && ImGui::GetFrameCount ( ) - SKIV_Viewer_AnimDrawnFrame <= 1)
    wakeIn = _WakeIn (SKIV_Viewer_AnimNextFrameAt);

  if (SKIV_Viewer_CarouselWakeAt != 0)
    wakeIn = std::min (wakeIn, _WakeIn (SKIV_Viewer_CarouselWakeAt));

  return wakeIn;
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
//   were made at), or the whole image. Wide gamut SDR images (scRGB) are made sRGB of 8 bpc, the colors outside of it
//     clipped (to be copied), unless scRGB is set (to be saved in their own colors, SKIV_Image_SaveWideGamutSDR).
static HRESULT
SKIV_CaptureImage (image_s& image, DirectX::ScratchImage& captured, bool scRGB = false)
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

  const bool wideSDR =
    (! image.is_hdr && (image.is_wcg || (image.anim != nullptr && image.anim->wide)));

  DirectX::ScratchImage texture;
  HRESULT hr =
    DirectX::CaptureTexture (pDevice, pDevCtx, pTexResource.p, texture);

  if (FAILED (hr))
    return hr;

  // An animated image: its frame's part of the page
  DirectX::ScratchImage frameImg;

  if (image.anim != nullptr)
  {
    const image_anim_s::frame_s& frame = image.anim->frames [image.anim->current];

    const size_t width  = static_cast <size_t> (image.anim->resolution.x),
                 height = static_cast <size_t> (image.anim->resolution.y);

    if (FAILED (hr = frameImg.Initialize2D (texture.GetMetadata ( ).format, width, height, 1, 1)) ||
        FAILED (hr = DirectX::CopyRectangle (*texture.GetImages ( ), DirectX::Rect (static_cast <size_t> (frame.offset.x), static_cast <size_t> (frame.offset.y), width, height),
                                             *frameImg.GetImages ( ), DirectX::TEX_FILTER_DEFAULT, 0, 0)))
      return hr;

    texture = std::move (frameImg);
  }

  // Wide gamut SDR (scRGB): as sRGB of 8 bpc, the colors outside of it clipped, unless asked for as it is
  if (wideSDR && ! scRGB)
    return DirectX::Convert (*texture.GetImages ( ), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DirectX::TEX_FILTER_DEFAULT, 0.0f, captured);

  captured = std::move (texture);
  return S_OK;
}

#define _WIDTH_SCROLLBAR (SKIF_vecAlteredSize.y > 0.0f ? ImGui::GetStyle().ScrollbarSize : 0.0f)
#define _WIDTH   (378.0f * SKIF_ImGui_GlobalDPIScale) - _WIDTH_SCROLLBAR // GameList, GameDetails, Injection_Summary_Frame (prev. 414.0f)
// 1038px == 415px
// 1000px == 377px (using 380px)

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



static void
_LogColorSource (const image_s& image)
{
  std::string detail = SKIV_DescribeColorSourceInDetail (image.color_source);
  std::replace (detail.begin (), detail.end (), '\n', ';');

  PLOG_INFO << "Color space: " << SKIV_DescribeColorSource (image.color_source) << " [" << detail << "]";
}



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
      last_scaling == s_view.scaling)
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

  ImageScaling _appliedScaling = s_view.scaling;

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
  if (last_scaling != s_view.scaling)
  {
    last_scaling    = s_view.scaling;
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

  if (++(uint8_t&)s_view.scaling >= ImageScaling_MaxValue)
                  s_view.scaling  = ImageScaling_Auto;

  ImGuiToast toast = {
    ImGuiToastType::Info, 2500,
      "Scaling Mode Changed",
      "New Mode:\t%hs",
        s_view.scaling == ImageScaling_Auto ? "Auto"        :
        s_view.scaling == ImageScaling_None ? "Actual Size" :
        s_view.scaling == ImageScaling_Fit  ? "Zoom to Fit" :
        s_view.scaling == ImageScaling_Fill ? "Fill Window" :
                                             "Invalid"
  };

  // Dismiss any previous mode change notifications that may still be visible
  if (last_toast_id >= 0)
  {
    ImGui::DismissNotificationById (last_toast_id);
  }

  last_toast_id =
    ImGui::InsertNotification (toast);

  return s_view.scaling;
}

#pragma region WheelZoom

// Zoom limits (1.0f = 100%)
static constexpr float SKIV_ZOOM_MAX = 5.0f;
static constexpr float SKIV_ZOOM_MIN = 0.05f;

// The image's layout in the previous frame, recorded by SKIF_UI_Tab_DrawViewer for the wheel zoom and image dragging
static struct {
  bool         valid     = false;
  ImGuiWindow* window    = nullptr; // The content frame the image is drawn in
  ImVec2       imagePos;            // Screen position of the drawn image
  ImVec2       imageSize;           // Its drawn (floored) size
  ImVec2       rawSize;             // Its size before flooring
  float        zoom      = 1.0f;    // The zoom it was drawn at
  ImVec2       originRel;           // Where an uncentered image starts, relative to DC.CursorStartPos
  bool         hovered   = false;   // The frame (or one of its children) was hovered, not blocked by a popup
  bool         imageHovered = false; // The image was hovered (and can be dragged or double-clicked)
  bool         emptyHovered = false; // The empty space around the image was hovered (not the image, nor e.g. the details)
  bool         smallImage   = false; // The image fits in the frame at its actual size (so "Automatic" shows it so)
} s_viewerLayout;

// Where the image being faded out (cover_old) was last shown, so it fades out there (rather than centered); captured
//   when it is swapped out (see _SwapOutCover in SKIF_UI_Tab_DrawViewer)
static struct {
  bool   valid    = false;
  ImVec2 position;          // Its top left corner, relative to the top left corner of the visible part of the frame
  ImVec2 size;              // Its drawn size
} s_fadeOutLayout;

// Set when the image scaling is changed to show the image at a position of our own (e.g. a double-click to its actual
//   size): keeps that change of scaling from centering the image (see GetCurrentAspectRatio and resetScrollCenter)
static bool s_keepPlacementOnRescale = false;

// After a wheel zoom or dragging the image, the image is placed where that put it (rather than centered, or kept
//   within the frame). This lasts until the zoom is changed some other way, the image scaling changes, another image
//   is shown, or the window is maximized or restored.
static struct {
  bool   active  = false;
  void*  image   = nullptr; // The image it applies to
  float  zoom    = 1.0f;    // The zoom it applies to
  ImVec2 offset;            // Empty space before the image (content coordinates, from originRel)
  ImVec2 content;           // The content size, relative to DC.CursorStartPos (includes empty space after the image)
} s_wheelLayout;

// The image is being dragged with the left mouse button (started by SKIF_UI_Tab_DrawViewer on a click on the image)
static bool s_imageDragActive = false;

// The image can be dragged only once it has been interacted with: clicked or double-clicked, or zoomed with the wheel
//   (it is then likely to be looked at closely). Until then, a drag on it moves the window, as one on the empty space
//   around it does: an image that covers the window would otherwise move at any attempt to move the window. Each image
//   shown starts out not engaged.
static void* s_imageEngaged      = nullptr; // The image engaged (cover's texture), if any
static void* s_imageEngagedShown = nullptr; // The image shown when that was last checked (another one ends it)
static bool  s_imagePressPending = false;   // A press on the image not engaged: a click (it engages it), or a drag (it
                                            //   moves the window)
static bool  s_windowMoveWanted  = false;   // Such a drag: the main loop starts moving the window (SKIV_Viewer_MoveWindowIfWanted)
static ImVec2 s_windowMoveFrom;             //   ... where it started (screen coordinates)

static bool
SKIV_Viewer_IsImageEngaged (void)
{
  if (s_imageEngagedShown != cover.pRawTexSRV.p)
  {
    s_imageEngagedShown = cover.pRawTexSRV.p;
    s_imageEngaged      = nullptr;
  }

  return (cover.pRawTexSRV.p != nullptr && s_imageEngaged == cover.pRawTexSRV.p);
}

static void
SKIV_Viewer_EngageImage (void)
{
  SKIV_Viewer_IsImageEngaged ( ); // (Forgets an image no longer shown)
  s_imageEngaged = cover.pRawTexSRV.p;
}

// Called by the main loop between frames (with the messages): a drag on an image not engaged moves the window, by the
//   window's own move loop (as dragging its caption does), until the mouse button is released. Not in the middle of a
//   frame, as the move loop runs the message loop (and holds everything else) until then.
void
SKIV_Viewer_MoveWindowIfWanted (void)
{
  if (! std::exchange (s_windowMoveWanted, false) || SKIF_ImGui_hWnd == NULL)
    return;

  // Still held (the left button: the primary one, if they are swapped)
  const int vk = ::GetSystemMetrics (SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;

  if (::GetAsyncKeyState (vk) & 0x8000)
  {
    // The window catches up with the mouse first: it moved some way before the drag was told apart from a click
    POINT cursor = { };
    RECT  rect   = { };

    if (! ::IsZoomed (SKIF_ImGui_hWnd) && ::GetCursorPos (&cursor) && ::GetWindowRect (SKIF_ImGui_hWnd, &rect))
      ::SetWindowPos (SKIF_ImGui_hWnd, NULL, rect.left + cursor.x - static_cast <LONG> (s_windowMoveFrom.x),
                                             rect.top  + cursor.y - static_cast <LONG> (s_windowMoveFrom.y), 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    ::ReleaseCapture ( );
    ::SendMessage    (SKIF_ImGui_hWnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
  }

  // The button was released in the move loop, which ImGui does not see
  ImGui::GetIO ( ).AddMouseButtonEvent (ImGuiMouseButton_Left, false);
}

// The thumbnails carousel (SKIV_Viewer_DrawCarousel): a strip of the images in the folder along the bottom of the frame
static struct {
  float  alpha    = 0.0f;       // Its fade (0: hidden)
  double lastNear = -1.0;       // When the mouse was last near it (ImGui::GetTime)
  float  scroll   = 0.0f;       // The index of the image in its middle (eases towards target)
  float  target   = 0.0f;
  size_t current  = SIZE_MAX;   // The index of the active image, when it was last drawn
  bool   backgroundHovered = false; // Its background (no tile) is hovered: acts as the window's caption (dragging, double-clicks)
  bool   hovered  = false;      // The mouse is over it: the wheel scrolls it (rather than zooming the image)
  std::unordered_map <std::wstring, double>
         seen;                  // When each thumbnail was first drawn (it fades in)
} s_carousel;

// True while the mouse is over the empty space around the image in the viewer, or the background of the thumbnails
//   carousel; the window procedure then treats a double-click there as one on the window's caption (maximizing or
//   restoring the window)
bool
SKIV_Viewer_IsEmptySpaceHovered (void)
{
  return SKIF_Tab_Selected == UITab_Viewer && (s_viewerLayout.emptyHovered || s_carousel.backgroundHovered);
}

// Places the image's top left corner at the given screen position in the frame about to begin, at the given size.
//   The scroll position (and the content size it is clamped against) are set for that frame, so the image moves
//   in the same frame its size or position changes.
static void
SKIV_Viewer_PlaceImage (const ImVec2& targetPos, const ImVec2& newSize)
{
  ImGuiWindow* window = s_viewerLayout.window;

  // The visible part of the frame, in content units
  const ImVec2 visible = window->InnerRect.GetSize ( ) - window->WindowPadding * 2.0f;

  ImVec2 offset, scroll, content;

  for (int axis = 0; axis < 2; axis++)
  {
    // On screen, the image is drawn at: base + offset - scroll
    //   (base: where an image without an offset starts at scroll 0; DC.CursorStartPos includes the previous frame's -scroll)
    const float basePos = window->DC.CursorStartPos [axis] + window->Scroll [axis] + s_viewerLayout.originRel [axis];
    const float delta   = ImFloor (targetPos [axis] - basePos + 0.5f);

    // Empty space before the image when it should start further in, scrolled past when it should start further out
    offset  [axis] = ImMax (0.0f,  delta);
    scroll  [axis] = ImMax (0.0f, -delta);

    // Enough content to reach the image's end, and to scroll as far as needed (empty space after the image)
    content [axis] = ImMax (s_viewerLayout.originRel [axis] + offset [axis] + newSize [axis], scroll [axis] + visible [axis]);
  }

  s_wheelLayout.active  = true;
  s_wheelLayout.image   = cover.pRawTexSRV.p;
  s_wheelLayout.zoom    = s_view.zoom;
  s_wheelLayout.offset  = offset;
  s_wheelLayout.content = content;

  // Enough content for this frame, so the new scroll position is not clamped against the old layout: extends what the
  //   previous frame measured (which Begin turns into the content size and scroll limits). Not SetNextWindowContentSize,
  //   as an explicit content size would also become the frame's available size, and with it the size "Zoom to fit" fits in.
  window->DC.CursorMaxPos = ImMax (window->DC.CursorMaxPos, window->DC.CursorStartPos + content);
  window->DC.IdealMaxPos  = ImMax (window->DC.IdealMaxPos,  window->DC.CursorStartPos + content);

  ImGui::SetNextWindowScroll (scroll);
}

// Dragging the image: moves it along with the mouse cursor, keeping some of it within the frame
static bool
SKIV_Viewer_HandleImageDrag (void)
{
  // A press on an image not engaged: released where it was, it was a click (which engages the image); moved further
  //   than a click would, the window is moved
  if (s_imagePressPending)
  {
    if (! ImGui::IsMouseDown (ImGuiMouseButton_Left))
    {
      s_imagePressPending = false;
      SKIV_Viewer_EngageImage ( );
    }

    else if (ImGui::IsMouseDragging (ImGuiMouseButton_Left))
    {
      s_imagePressPending = false;

      extern bool SKIF_MouseDragMoveAllowed; // (Not in fullscreen or snipping mode)
      s_windowMoveWanted  = SKIF_MouseDragMoveAllowed;
      s_windowMoveFrom    = ImGui::GetIO ( ).MouseClickedPos [ImGuiMouseButton_Left];
    }

    return false;
  }

  if (! s_imageDragActive)
    return false;

  if (! ImGui::IsMouseDown (ImGuiMouseButton_Left))
  {
    s_imageDragActive = false;
    return false;
  }

  const ImVec2 delta = ImGui::GetIO ( ).MouseDelta;

  if (delta.x == 0.0f && delta.y == 0.0f)
    return false;

  const ImRect inner  = s_viewerLayout.window->InnerRect;
  const ImVec2 size   = s_viewerLayout.imageSize;
  const ImVec2 keep   = ImMin (size, ImVec2 (48.0f, 48.0f) * SKIF_ImGui_GlobalDPIScale);
        ImVec2 target = s_viewerLayout.imagePos + delta;

  target.x = ImClamp (target.x, inner.Min.x + keep.x - size.x, inner.Max.x - keep.x);
  target.y = ImClamp (target.y, inner.Min.y + keep.y - size.y, inner.Max.y - keep.y);

  SKIV_Viewer_PlaceImage (target, size);

  return true;
}

// Zooming with the mouse wheel, keeping the pixel of the image under the mouse cursor in place
static bool
SKIV_Viewer_HandleWheelZoom (void)
{
  ImGuiIO& io = ImGui::GetIO ( );

  if (io.MouseWheel == 0.0f)
    return false;

  // Plain wheel: over the image viewer only (not menus, popups or other windows), and not while e.g. dragging
  // Ctrl+wheel:  zooms wherever the mouse is, as it always has
  const bool bOverViewer = s_viewerLayout.hovered && ImGui::IsMousePosValid ( );

  if (! io.KeyCtrl && (! bOverViewer || ImGui::IsAnyItemActive ( )))
    return false;

  // Over the thumbnails carousel, the wheel scrolls it
  if (! io.KeyCtrl && s_carousel.hovered)
    return false;

  const float oldZoom = s_view.zoom;
  float       newZoom = std::clamp (oldZoom * powf (1.1f, io.MouseWheel), SKIV_ZOOM_MIN, SKIV_ZOOM_MAX);

  // Stop at 100% on the way past it
  if ((oldZoom < 1.0f && newZoom > 1.0f) || (oldZoom > 1.0f && newZoom < 1.0f))
    newZoom = 1.0f;

  if (newZoom == oldZoom)
    return false;

  s_view.zoom = newZoom;

  // Zoomed: likely to be looked at closely (dragged around)
  SKIV_Viewer_EngageImage ( );

  // The point kept in place: the mouse cursor, or the middle of the frame when Ctrl+wheel is used elsewhere
  const ImVec2 anchor = bOverViewer ? io.MousePos : s_viewerLayout.window->InnerRect.GetCenter ( );

  // Where in the image (0-1) that point is. While the wheel keeps turning with the mouse in place, the point found
  //   at its first turn is kept, so rounding to whole pixels at each turn does not add up.
  static ImVec2 gestureAnchor   (-FLT_MAX, -FLT_MAX);
  static ImVec2 gestureFraction;
  static double gestureTime     = 0.0;
  static void*  gestureImage    = nullptr;

  ImVec2 fraction = (anchor - s_viewerLayout.imagePos) / ImMax (s_viewerLayout.imageSize, ImVec2 (1.0f, 1.0f));

  if (ImLengthSqr (anchor - gestureAnchor) <= 4.0f && ImGui::GetTime ( ) - gestureTime < 1.0 && gestureImage == cover.pRawTexSRV.p)
    fraction = gestureFraction;
  else
  {
    gestureAnchor   = anchor;
    gestureFraction = fraction;
    gestureImage    = cover.pRawTexSRV.p;
  }

  gestureTime = ImGui::GetTime ( );

  // The image's size in the coming frame (as SKIF_UI_Tab_DrawViewer will calculate it)
  const ImVec2 newSize = ImFloor (s_viewerLayout.rawSize * (newZoom / s_viewerLayout.zoom));

  // Its top left corner lands at: anchor - fraction * newSize
  SKIV_Viewer_PlaceImage (anchor - fraction * newSize, newSize);

  return true;
}

// Double-clicking the image switches between "Automatic" (actual size if smaller than the window, otherwise zoom to
//   fit) and, for an image larger than the window, "View actual size" (1:1), keeping the pixel under the mouse cursor
//   in place, or, for one smaller than the window (which "Automatic" already shows at 1:1), "Zoom to fit"
static bool
SKIV_Viewer_HandleImageDoubleClick (void)
{
  if (! ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left) || ImGui::GetIO ( ).KeyCtrl || ! s_viewerLayout.imageHovered)
    return false;

  s_imageDragActive   = false; // (The second click of the double-click started a drag)
  s_imagePressPending = false;

  SKIV_Viewer_EngageImage ( );

  // By the scaling (not by how the image is shown: "Automatic" shows an image smaller than the window as "View actual
  //   size" does, and one larger than it as "Zoom to fit" does)
  const ImVec2       actualSize (cover.width, cover.height);
  const ImageScaling other    = (s_viewerLayout.smallImage) ? ImageScaling_Fit : ImageScaling_None;
  const bool         toAuto   = (s_view.scaling == other && s_view.zoom == 1.0f);

  s_view.zoom = 1.0f;

  // Back to "Automatic", or a small image fitted to the window: centered
  if (toAuto || other == ImageScaling_Fit)
  {
    s_view.scaling        = (toAuto) ? ImageScaling_Auto : ImageScaling_Fit;
    s_wheelLayout.active = false;
    resetScrollCenter    = true;
  }

  // Otherwise its actual size ("View actual size" does not apply the DPI scale)
  else
  {
    const ImVec2 anchor   = ImGui::GetIO ( ).MousePos;
    const ImVec2 fraction = (anchor - s_viewerLayout.imagePos) / ImMax (s_viewerLayout.imageSize, ImVec2 (1.0f, 1.0f));

    s_view.scaling            = ImageScaling_None;
    s_keepPlacementOnRescale = true;

    SKIV_Viewer_PlaceImage (anchor - fraction * actualSize, actualSize);
  }

  return true;
}

// Called each frame before the viewer's content frame begins: the mouse wheel zoom, dragging and double-clicking the
//   image, and recentering the image when the window is maximized or restored
void
SKIV_Viewer_HandleMouseZoomPan (void)
{
  // Maximizing or restoring the window (or toggling fullscreen) centers the image: one placed by a zoom or a drag,
  //   and one larger than the frame (e.g. still zoomed in when the window is restored). The frame gets its new size
  //   a few frames after the window does, so the image is kept centered for a while, until its size stops changing.
  static int    lastWindowState = -1;
  static bool   recenter        = false;
  static double recenterSince   = 0.0;
  static int    recenterStable  = 0;
  static ImVec2 recenterVisible;

  // (Minimizing is not a change: restoring from the taskbar returns the window to the state it had)
  if (SKIF_ImGui_hWnd != NULL && ! ::IsIconic (SKIF_ImGui_hWnd))
  {
    const int windowState = SKIF_ImGui_IsFullscreen (SKIF_ImGui_hWnd) ? 2 :
                                         ::IsZoomed (SKIF_ImGui_hWnd) ? 1 : 0;

    if (lastWindowState != -1 && windowState != lastWindowState)
    {
      s_wheelLayout.active = false; // Back to the image's usual (centered) layout
      s_imageDragActive    = false;
      recenter             = true;
      recenterSince        = ImGui::GetTime ( );
      recenterStable       = 0;
      recenterVisible      = ImVec2 (-1.0f, -1.0f);
    }

    lastWindowState = windowState;
  }

  // Double-clicking the empty space around the image maximizes or restores the window. Usually the window procedure
  //   sees that double-click (as the empty space acts as the window's caption, see SKIF_ImGui_CanMouseDragMove);
  //   this is for when it does not, e.g. on touch devices.
  if (ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left) && ! ImGui::GetIO ( ).KeyCtrl && SKIV_Viewer_IsEmptySpaceHovered ( ))
  {
    extern bool SKIF_ImGui_CaptionDoubleClicked;
    SKIF_ImGui_CaptionDoubleClicked = true;
  }

  if (cover.pRawTexSRV.p == nullptr || ! s_viewerLayout.valid || s_viewerLayout.window == nullptr)
  {
    s_imageDragActive   = false;
    s_imagePressPending = false;
    recenter          = false;
    return;
  }

  // One of these per frame, as each sets the layout of the coming frame; any of them ends a recentering
  if (SKIV_Viewer_HandleImageDoubleClick ( ) || SKIV_Viewer_HandleImageDrag ( ) || SKIV_Viewer_HandleWheelZoom ( ))
    recenter = false;

  else if (recenter)
  {
    ImGuiWindow* window  = s_viewerLayout.window;
    const ImVec2 visible = window->InnerRect.GetSize ( ) - window->WindowPadding * 2.0f;

    // An image larger than the frame starts at originRel; one smaller than it is centered by its usual layout (no scroll)
    ImGui::SetNextWindowScroll (ImMax (ImVec2 (0.0f, 0.0f),
                                       ImFloor (s_viewerLayout.originRel + (s_viewerLayout.imageSize - visible) * 0.5f)));

    recenterStable  = (visible.x == recenterVisible.x && visible.y == recenterVisible.y) ? recenterStable + 1 : 0;
    recenterVisible =  visible;

    if (recenterStable >= 3 && ImGui::GetTime ( ) - recenterSince >= 0.5)
      recenter = false;
  }
}

#pragma endregion

// The thumbnails carousel: a strip of the images in the folder along the bottom of the frame (the current window), over
//   the image. The thumbnails are the image store's; those of the tiles in view are asked for (the nearest last, so that
//   they are made first). Shown always, or while the mouse is near the bottom of the frame (Settings: Thumbnails
//   carousel). A click on a tile shows its image, the wheel over it scrolls it, and it follows the image shown.
//   Returns the file of a tile clicked (empty if none).
static std::wstring
SKIV_Viewer_DrawCarousel (skiv_image_directory_s::fl_s& files, SKIV_ImageStore& store)
{
  ImGuiIO&     io     = ImGui::GetIO ( );
  ImGuiWindow* frame  = ImGui::GetCurrentWindow ( );
  const int    mode   = _registry.iThumbnailCarousel; // 0: never, 1: when the mouse is nearby, 2: always
  const size_t count  = files.count ( );
  const size_t active = files.activeIndex ( );
  const double now    = ImGui::GetTime ( );

  SKIV_Viewer_CarouselWakeAt = 0;
  s_carousel.hovered           = false;
  s_carousel.backgroundHovered = false;

  auto _Hide = [&](void) -> std::wstring
  {
    s_carousel.alpha    = 0.0f;
    s_carousel.lastNear = -1.0;
    return L"";
  };

  // For an image in a folder of others (the image being loaded is the active one already). It stays while the next one
  //   loads, when none is shown (the one shown was swapped out, and may be fading out meanwhile).
  const bool loading = loadImage || tryingToLoadImage || store.IsLoading ( ) || cover_old.pRawTexSRV.p != nullptr;

  if (mode == 0 || count < 2 || active >= count || files.at (active).path.empty ( ) || (cover.file_info.path.empty ( ) && ! loading))
    return _Hide ( );

  // Its size follows the window's height: tiles of 32 px at 720 px (or less), up to the thumbnails' own size
  const float  height  = ImGui::GetWindowViewport ( )->Size.y;
  const float  tile    = std::clamp (std::round (height * (32.0f / 720.0f)), 32.0f, static_cast <float> (SKIV_THUMBNAIL_SIZE));
  const float  gap     = std::round (tile * 0.125f);
  const float  pad     = std::round (tile * 0.1875f);
  const float  step    = tile + gap;
  const ImRect inner   = frame->InnerRect;
  const ImRect strip (ImVec2 (inner.Min.x, inner.Max.y - tile - pad * 2.0f), inner.Max);

  if (strip.GetWidth ( ) < tile || strip.Min.y < inner.Min.y)
    return _Hide ( );

  // When it shows: always, or while the mouse is nearby it (and for a moment after), not while the image is dragged
  bool show = (mode == 2);

  if (mode == 1)
  {
    static constexpr double s_linger = 0.6; // s

    const ImRect zone (ImVec2 (strip.Min.x, strip.Min.y - strip.GetHeight ( ) * 0.5f), strip.Max);
    const bool   nearby = ImGui::IsMousePosValid ( ) && zone.Contains (io.MousePos) && ! s_imageDragActive &&
                        ImGui::IsWindowHovered (ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (nearby)
      s_carousel.lastNear = now;

    show = nearby || (s_carousel.lastNear >= 0.0 && now - s_carousel.lastNear < s_linger);

    // The main loop sleeps while nothing changes: woken to fade it out, and while it shows, now and then to see whether
    //   the mouse is still near it (the window may get no message once the mouse has left it)
    if (show && ! nearby)
      SKIV_Viewer_CarouselWakeAt = SKIF_Util_timeGetTime1 ( ) + static_cast <DWORD> ((s_linger - (now - s_carousel.lastNear)) * 1000.0) + 1;
    else if (show)
      SKIV_Viewer_CarouselWakeAt = SKIF_Util_timeGetTime1 ( ) + 250;
  }

  s_carousel.alpha = (show) ? std::min (1.0f, s_carousel.alpha + io.DeltaTime / 0.15f)
                            : std::max (0.0f, s_carousel.alpha - io.DeltaTime / 0.25f);

  // It follows the image shown (the wheel may have scrolled it away)
  const int half = static_cast <int> (std::ceil (strip.GetWidth ( ) * 0.5f / step)) + 1; // Tiles each way from the middle

  if (active != s_carousel.current)
  {
    s_carousel.target  = static_cast <float> (active);
    s_carousel.current = active;

    // Far away (another folder, or hidden meanwhile): there right away
    if (std::abs (s_carousel.scroll - s_carousel.target) > half || s_carousel.alpha <= 0.0f)
      s_carousel.scroll = s_carousel.target;
  }

  if (s_carousel.alpha <= 0.0f)
  {
    s_carousel.scroll  = s_carousel.target = static_cast <float> (active);
    return L"";
  }

  // Easing towards where it is scrolled to
  if (s_carousel.scroll != s_carousel.target)
  {
    s_carousel.scroll += (s_carousel.target - s_carousel.scroll) * (1.0f - std::exp (-io.DeltaTime * 14.0f));

    if (std::abs (s_carousel.target - s_carousel.scroll) < 0.002f)
      s_carousel.scroll = s_carousel.target;
  }

  std::wstring clicked;

  // A child window over the image (and the details): its items do not reach the image's drag, double-click and wheel.
  //   It is not content of the frame: the frame's content size is kept as it was (it would hold the frame's scrolling).
  const ImVec2 parentPos  = ImGui::GetCursorPos ( );
  const ImVec2 savedMax   = frame->DC.CursorMaxPos;
  const ImVec2 savedIdeal = frame->DC.IdealMaxPos;

  ImGui::SetCursorScreenPos (strip.Min);
  ImGui::PushStyleVar       (ImGuiStyleVar_WindowPadding, ImVec2 (0.0f, 0.0f));
  ImGui::PushStyleVar       (ImGuiStyleVar_Alpha, ImGui::GetStyle ( ).Alpha * s_carousel.alpha);

  if (ImGui::BeginChild ("###SKIV_Carousel", strip.GetSize ( ), ImGuiChildFlags_None,
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground |
                         ImGuiWindowFlags_NoNav       | ImGuiWindowFlags_NoSavedSettings))
  {
    ImDrawList*  dl       = ImGui::GetWindowDrawList ( );
    const float  centerX  = std::floor (strip.GetCenter ( ).x);
    const float  top      = strip.Min.y + pad;
    const float  rounding = std::max (2.0f, std::round (tile * 0.06f));
    const float  border   = std::max (1.0f, std::round (tile / 24.0f));

    dl->AddRectFilled (strip.Min, strip.Max, ImGui::GetColorU32 (ImGuiCol_WindowBg, 0.8f));

    // Each tile is an item of its own: a click on one (released on it) shows its image, and does not reach the image or
    //   move the window. The background around them is no item, so that, as the invisible title bar along the top of
    //   the window does, dragging it moves the window (SKIF_ImGui_CanMouseDragMove) and double-clicking it maximizes or
    //   restores the window (SKIV_Viewer_IsEmptySpaceHovered).
    const bool stripHovered = ImGui::IsWindowHovered (ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    int        hoveredTile  = -1;

    s_carousel.hovered = stripHovered;

    // The wheel scrolls it (either wheel; > 0: towards the first image)
    if (stripHovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f))
      s_carousel.target = std::clamp (s_carousel.target - io.MouseWheel - io.MouseWheelH, 0.0f, static_cast <float> (count - 1));

    // The tiles in view: the outermost first, so that the nearest are asked for last (their thumbnails are made first)
    static std::vector <int> tiles;
    tiles.clear ( );

    for (int i = std::max (0, static_cast <int> (std::floor (s_carousel.scroll)) - half);
             i <= std::min (static_cast <int> (count) - 1, static_cast <int> (std::ceil (s_carousel.scroll)) + half); i++)
      tiles.push_back (i);

    std::sort (tiles.begin ( ), tiles.end ( ), [&](int a, int b) {
      return std::abs (a - s_carousel.scroll) > std::abs (b - s_carousel.scroll); });

    // The reserved texture coordinates the shaders take for HDR content and sRGB (DDS) content (see GameCover)
    static const ImVec2 hdr_uv0  (-1024.0f, -1024.0f), hdr_uv1  (-2048.0f, -2048.0f),
                        srgb_uv0 (-4096.0f, -4096.0f), srgb_uv1 (-8192.0f, -8192.0f);

    if (s_carousel.seen.size ( ) > 1024)
      s_carousel.seen.clear ( );

    // The thumbnails are drawn smaller than they are: sampled from their mipmaps (set back to the usual sampler below)
    extern void SKIV_ImplDX11_DrawCallback_SetSamplerMipmapped (const ImDrawList*, const ImDrawCmd*);
    dl->AddCallback (SKIV_ImplDX11_DrawCallback_SetSamplerMipmapped, nullptr);

    for (const int i : tiles)
    {
      const float  x0 = std::floor (centerX + (i - s_carousel.scroll) * step - tile * 0.5f);
      const ImRect r  (ImVec2 (x0, top), ImVec2 (x0 + tile, top + tile));

      if (r.Max.x <= strip.Min.x || r.Min.x >= strip.Max.x)
        continue;

      const std::wstring& path = files.at (i).path;

      ImGui::PushID (i);

      const ImGuiID id          = ImGui::GetID ("###SKIV_CarouselTile");
      bool          tileHovered = false,
                    tileHeld    = false;

      if (ImGui::ItemAdd (r, id, nullptr, ImGuiItemFlags_NoNav))
      {
        if (ImGui::ButtonBehavior (r, id, &tileHovered, &tileHeld) && i != static_cast <int> (active))
          clicked = path;

        // Its file name
        if (tileHovered)
        {
          hoveredTile = i;
          SKIF_ImGui_SetHoverTip (SK_WideCharToUTF8 (files.at (i).filename), false, false, true);
        }
      }

      ImGui::PopID ( );

      // The border of the active tile (and of the one hovered): a filled shape around the tile, which the tile is drawn
      //   over. Not an outline, as ImGui draws thick outlines wrongly at corners rounded by less than their thickness.
      if (i == static_cast <int> (active) || i == hoveredTile)
      {
        const float width = (i == static_cast <int> (active)) ? border * 2.0f : std::round (border * 1.5f);

        dl->AddRectFilled (r.Min - ImVec2 (width, width), r.Max + ImVec2 (width, width),
                           ImGui::GetColorU32 ((i == static_cast <int> (active)) ? ImGuiCol_SKIF_GridActive : ImGuiCol_SKIF_GridHovered),
                           rounding + width);
      }

      dl->AddRectFilled (r.Min, r.Max, ImGui::GetColorU32 (ImGuiCol_FrameBg), rounding);

      if (const image_s* thumbnail = store.Thumbnail (path, true);
          thumbnail != nullptr && thumbnail->pRawTexSRV.p != nullptr && thumbnail->width > 0.0f && thumbnail->height > 0.0f)
      {
        const double since  = s_carousel.seen.try_emplace (path, now).first->second;
        const float  fadeIn = std::clamp (static_cast <float> ((now - since) / 0.15), 0.0f, 1.0f);

        // Fitted to the tile
        const float  scale = std::min (tile / thumbnail->width, tile / thumbnail->height);
        const ImVec2 size  = ImMax (ImFloor (ImVec2 (thumbnail->width, thumbnail->height) * scale), ImVec2 (1.0f, 1.0f));
        const ImVec2 pos   = r.Min + ImFloor ((ImVec2 (tile, tile) - size) * 0.5f);

        // HDR ones by the shaders' HDR path (tone mapped as the image shown is) while the image shown is drawn by it too
        //   (its light levels set up that path), and no visualization is shown (it would apply to them too, and the
        //   gamut coverage of the image shown would take in theirs)
        const bool hdr  = thumbnail->light_info.isHDR && SKIV_Viewer_DrawAsHDR (cover) &&
                          SKIV_HDR_VisualizationId == SKIV_HDR_VISUALIZTION_NONE;
        const bool srgb = ! hdr && thumbnail->is_dds;

        dl->AddImage ((ImTextureID)(intptr_t)thumbnail->pRawTexSRV.p, pos, pos + size,
                      hdr ? hdr_uv0 : srgb ? srgb_uv0 : ImVec2 (0.0f, 0.0f),
                      hdr ? hdr_uv1 : srgb ? srgb_uv1 : ImVec2 (1.0f, 1.0f),
                      ImGui::GetColorU32 (ImVec4 (1.0f, 1.0f, 1.0f, fadeIn)));

        // Fading in: drawn again until it has
        if (fadeIn < 1.0f)
          SKIV_Viewer_CarouselWakeAt = SKIF_Util_timeGetTime1 ( );
      }
    }

    dl->AddCallback (ImGui::GetPlatformIO ( ).DrawCallback_SetSamplerLinear, nullptr);

    // Its background (no tile), not while something else is held (e.g. the image, dragged over it)
    s_carousel.backgroundHovered = stripHovered && hoveredTile < 0 && ! ImGui::IsAnyItemActive ( );
  }

  ImGui::EndChild     ( );
  ImGui::PopStyleVar  (2);
  ImGui::SetCursorPos (parentPos);

  frame->DC.CursorMaxPos = savedMax;
  frame->DC.IdealMaxPos  = savedIdeal;

  return clicked;
}

// Main UI function

void
SKIF_UI_Tab_DrawViewer (void)
{
  extern bool imageFadeActive;

  SKIV_ImageStore& _store = SKIV_ImageStore::Get ( );

  // Notifications of image decodes (from their worker threads)
  {
    ImGuiToast notice (ImGuiToastType::None);

    while (_store.TakeNotice (notice))
      ImGui::InsertNotification (notice);
  }

  // The image being loaded, once it has been decoded: shown from now on
  {
    bool success = false;

    if (_store.TakeResult (cover, success))
    {
      if (success)
        _LogColorSource (cover);

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

      // We have stopped loading the image (as WM_SKIF_IMAGE used to note, once the worker had swapped it in)
      imageLoading.store (false);
      tryingToLoadImage = false;
      tryingToDownImage = false;

      // Empty the working set after the image has finished loading
      SKIF_Util_CompactWorkingSet ( );

      if (success)
        newImageLoaded = true;
      else
        newImageFailed = true;
    }
  }

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
      SKIV_ImageStore::Release (cover_old);

      // Set up the current one to be released
      cover_old = cover;
      cover.reset();

      // It fades out where it was last shown (s_viewerLayout is of the frame that last drew it)
      s_fadeOutLayout.valid = (cover_old.pRawTexSRV.p != nullptr && s_viewerLayout.valid && s_viewerLayout.window != nullptr);

      if (s_fadeOutLayout.valid)
      {
        s_fadeOutLayout.position = s_viewerLayout.imagePos - s_viewerLayout.window->InnerRect.Min;
        s_fadeOutLayout.size     = s_viewerLayout.imageSize;
      }

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
    SKIV_ImageStore::Release (cover_old);

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

    // The image shown and the ones around it in its folder: those next to it kept decoded by the image store (preloaded),
    //   so switching to them is quick, and the thumbnails of the nearest ones made ahead
    {
      auto*      active = _current_folder.fileList.getActiveFile ( );
      const bool listed = (active != nullptr && ! cover.file_info.path.empty ( ) && active->path == cover.file_info.path);

      static std::vector <std::wstring> around;
      around.clear ( );

      // Nearest first: the next one, the previous one, the one after the next, ...
      if (listed)
      {
        for (int distance = 1; distance <= SKIV_THUMBNAIL_REACH; distance++)
        {
          around.push_back (_current_folder.fileList.peek ( distance));
          around.push_back (_current_folder.fileList.peek (-distance));
        }
      }

      _store.SetNeighbours (cover.file_info.path, around, _registry.bAnimatedImages, _registry.iThumbnailCarousel != 0);
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
          s_view.scaling = (s_view.scaling != image_scaling) ? image_scaling : ImageScaling_Auto;
          s_view.zoom    = 1.0f;
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

  // Whether the image fits in the frame at its actual size, so "Automatic" shows it at 1:1 (as in GetCurrentAspectRatio)
  {
    const ImVec2 availAuto = ImFloor (ImGui::GetContentRegionAvail ( ) / SKIF_ImGui_GlobalDPIScale);

    s_viewerLayout.smallImage = (availAuto.x > cover.width && availAuto.y > cover.height);
  }

  // This allows images to be DPI-scaled on HiDPI displays up until the user uses "View actual size" to force them to appear as 100%
  ImVec2 sizeCover     = GetCurrentAspectRatio (cover)     * ((s_view.scaling == ImageScaling_None) ? 1 : SKIF_ImGui_GlobalDPIScale) * s_view.zoom;
  ImVec2 sizeCover_old = GetCurrentAspectRatio (cover_old) * ((s_view.scaling == ImageScaling_None) ? 1 : SKIF_ImGui_GlobalDPIScale) * s_view.zoom;

  // From now on ImGui UI calls starts being made...

#pragma region GameCover

  static const ImVec2 hdr_uv0  (-1024.0f, -1024.0f);
  static const ImVec2 hdr_uv1  (-2048.0f, -2048.0f);
  static const ImVec2 srgb_uv0 (-4096.0f, -4096.0f);
  static const ImVec2 srgb_uv1 (-8192.0f, -8192.0f);

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

  // (No image is loading, and none is shown: e.g. closed, while the previous one fades out)
  else if (! _store.IsLoading ( ) && cover.pRawTexSRV.p == nullptr)
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
    ImGuiWindow* window = ImGui::GetCurrentWindow ( );

    // The content extents before it, to be restored after it: the image fading out is not part of the content
    //   (it should not change the scroll limits of the image fading in)
    const ImVec2 cursorMaxPos = window->DC.CursorMaxPos;
    const ImVec2 idealMaxPos  = window->DC.IdealMaxPos;

    // Where it was last shown, regardless of the scroll position (which is now that of the image fading in)
    if (s_fadeOutLayout.valid)
    {
      ImGui::SetCursorScreenPos (window->InnerRect.Min + s_fadeOutLayout.position);
      sizeCover_old = s_fadeOutLayout.size;
    }

    // Otherwise centered, when smaller than the frame
    else
    {
      if (sizeCover_old.x < ImGui::GetContentRegionAvail().x)
        ImGui::SetCursorPosX (ImFloor ((ImGui::GetContentRegionAvail().x - sizeCover_old.x) * 0.5f));
      if (sizeCover_old.y < ImGui::GetContentRegionAvail().y)
        ImGui::SetCursorPosY (ImFloor ((ImGui::GetContentRegionAvail().y - sizeCover_old.y) * 0.5f));
    }

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

    if (s_fadeOutLayout.valid)
    {
      window->DC.CursorMaxPos = cursorMaxPos;
      window->DC.IdealMaxPos  = idealMaxPos;
    }

    ImGui::SetCursorPos (originalPos);

    fading = true;
  }

  bIsHDR =
    cover.is_hdr || cover.is_wcg; // Both have a gamut coverage chart

  // The gamut coverage is not computed during a cross-fade (both images are drawn then), but its chart stays known
  //   to the renderer by its SRV: that is what draws it opaque and without the checkerboard (it would otherwise be
  //   drawn as an image with transparency, showing the checkerboard while it has not been computed yet)
  SKIV_HDR_GamutCoverageUAV = (bIsHDR && ! fading) ? cover.pGamutCoverageUAV : nullptr;
  SKIV_HDR_GamutCoverageSRV = (bIsHDR)             ? cover.pGamutCoverageSRV : nullptr;

  SKIV_HDR_MaxCLL          = cover.light_info.max_cll;
  SKIV_HDR_MaxLuminanceP99 = cover.light_info.p99_nits;
  SKIV_HDR_MaxLuminance    = cover.light_info.max_nits;

  // For the wheel zoom (SKIV_Viewer_HandleMouseZoomPan): the image's unscaled size
  const ImVec2 rawSizeCover = sizeCover;

  // A change of scaling that placed the image itself (by the double-click to its actual size) does not center it
  if (s_keepPlacementOnRescale)
  {
    s_keepPlacementOnRescale = false;
    resetScrollCenter        = false;
  }

  // The layout of a wheel zoom ends when the zoom is changed some other way, the scaling changes or another image is shown
  if (s_wheelLayout.active && (resetScrollCenter                            ||
                               s_view.zoom        != s_wheelLayout.zoom      ||
                               cover.pRawTexSRV.p != s_wheelLayout.image))
    s_wheelLayout.active = false;

  // After a wheel zoom or a drag: where that placed the image
  if (s_wheelLayout.active)
    ImGui::SetCursorPos (originalPos + s_wheelLayout.offset);

  // Otherwise centered, when smaller than the frame
  else
  {
    if (sizeCover.x < ImGui::GetContentRegionAvail().x)
      ImGui::SetCursorPosX (ImFloor ((ImGui::GetContentRegionAvail().x - sizeCover.x) * 0.5f));
    if (sizeCover.y < ImGui::GetContentRegionAvail().y)
      ImGui::SetCursorPosY (ImFloor ((ImGui::GetContentRegionAvail().y - sizeCover.y) * 0.5f));
  }

  if (_registry._RendererHDREnabled)
    SKIV_HDR_MaxCLL = 1.0f;

  sizeCover         = ImFloor (sizeCover);

  // Centering a new image (or one with a new scaling) that is larger than the frame, by scrolling to its middle: the
  //   scroll position only applies from the next frame, so in this one the image is drawn where that will put it.
  //   Right away, also while the previous image fades out, as that one is drawn where it was (see s_fadeOutLayout)
  //   and is not part of the content; otherwise not until it has faded out (it would move along with the scroll).
  const bool   centerNow    = resetScrollCenter && cover.pRawTexSRV.p != nullptr &&
                              (cover_old.pRawTexSRV.p == nullptr || s_fadeOutLayout.valid);
  ImVec2       centerScroll,
               centerShift;  // How much further the image is drawn this frame than its layout position
  const ImVec2 cursorMaxPos = ImGui::GetCurrentWindow ( )->DC.CursorMaxPos;
  const ImVec2 idealMaxPos  = ImGui::GetCurrentWindow ( )->DC.IdealMaxPos;

  if (centerNow)
  {
    ImGuiWindow* window     = ImGui::GetCurrentWindow ( );
    const ImVec2 localStart = window->DC.CursorStartPos - window->Pos + window->Scroll;
    const ImVec2 visible    = window->InnerRect.GetSize ( ) - window->WindowPadding * 2.0f;
    const ImVec2 imageLocal = ImGui::GetCursorPos ( );

    // (An axis where the image is smaller than the frame is centered by the layout above, without scrolling)
    centerScroll = ImMax (ImVec2 (0.0f, 0.0f), ImFloor (imageLocal - localStart + (sizeCover - visible) * 0.5f));
    centerShift  = window->Scroll - centerScroll;

    ImGui::SetCursorPos (imageLocal + centerShift);
  }

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

  isImageHovered = ImGui::IsItemHovered() || s_imageDragActive; // (Being dragged makes the image item seem not hovered)

  // Centered right away (see above): the image counts as content at its layout position, and the frame is scrolled
  //   to where it was drawn
  if (centerNow)
  {
    ImGuiWindow* window = ImGui::GetCurrentWindow ( );

    window->DC.CursorMaxPos = ImMax (cursorMaxPos, image_rect.Max - centerShift);
    window->DC.IdealMaxPos  = ImMax (idealMaxPos,  image_rect.Max - centerShift);

    ImGui::SetScrollX (centerScroll.x);
    ImGui::SetScrollY (centerScroll.y);

    resetScrollCenter = false;
  }

  {
    ImGuiWindow* window = ImGui::GetCurrentWindow ( );
    const ImVec2 localStart = window->DC.CursorStartPos - window->Pos + window->Scroll;

    // After a wheel zoom or a drag: the empty space after the image that lets it be scrolled to where that put it
    if (s_wheelLayout.active)
    {
      const ImVec2 afterImage = ImGui::GetCursorPos ( );

      ImGui::SetCursorPos (localStart + s_wheelLayout.content);
      ImGui::Dummy        (ImVec2 (0.0f, 0.0f));
      ImGui::SetCursorPos (afterImage);
    }

    // Record the layout for the next frame's wheel zoom and image dragging
    s_viewerLayout.valid     = (cover.pRawTexSRV.p != nullptr);
    s_viewerLayout.window    = window;
    s_viewerLayout.imagePos  = image_pos;
    s_viewerLayout.imageSize = sizeCover;
    s_viewerLayout.rawSize   = rawSizeCover;
    s_viewerLayout.zoom      = s_view.zoom;
    s_viewerLayout.originRel = originalPos - localStart;
    s_viewerLayout.hovered   = ImGui::IsWindowHovered (ImGuiHoveredFlags_ChildWindows);

    // The empty space: the frame itself (not e.g. the details panel, a child window), outside the image
    s_viewerLayout.emptyHovered = ImGui::IsWindowHovered ( ) && ! ImGui::IsMouseHoveringRect (image_rect.Min, image_rect.Max);
    s_viewerLayout.imageHovered = false; // (Set by the image's drag item below)
  }

  // Reset scroll (center-align the scroll)
  if (resetScrollCenter && cover_old.pRawTexSRV.p == nullptr)
  {
    PLOG_VERBOSE << "Attempted to reset scroll...";

    resetScrollCenter = false;

    ImGui::SetScrollHereY ( );
    ImGui::SetScrollHereX ( );
  }

  // Dragging the image: a click on its visible part (not the empty space around it) starts it, and the drag itself is
  //   handled by SKIV_Viewer_HandleImageDrag before each frame. As an item, the image also keeps the click from moving
  //   the window (see SKIF_ImGui_CanMouseDragMove). Ctrl+drag stays the selection of a region of the image.
  //   (After the scroll reset above, which centers on the last item: the whole image, not just its visible part)
  if (cover.pRawTexSRV.p != nullptr && (! ImGui::GetIO ( ).KeyCtrl || s_imageDragActive))
  {
          ImRect  dragRect (ImMax (image_rect.Min, ImGui::GetCurrentWindow ( )->InnerClipRect.Min),
                            ImMin (image_rect.Max, ImGui::GetCurrentWindow ( )->InnerClipRect.Max));

    // Not the invisible title bar along the top of the window (see SKIF_ImGui_CaptionArea), which moves the window
    extern ImRect SKIF_ImGui_CaptionArea;
    if (SKIF_ImGui_CaptionArea.GetHeight ( ) > 0.0f && dragRect.Overlaps (SKIF_ImGui_CaptionArea))
      dragRect.Min.y = ImMax (dragRect.Min.y, SKIF_ImGui_CaptionArea.Max.y);


    const ImGuiID dragId = ImGui::GetCurrentWindow ( )->GetID ("###SKIV_ImageDrag");

    if (dragRect.GetWidth ( ) > 0.0f && dragRect.GetHeight ( ) > 0.0f && ImGui::ItemAdd (dragRect, dragId, nullptr, ImGuiItemFlags_NoNav))
    {
      bool hovered = false,
           held    = false;

      // Only once the image has been interacted with (see s_imageEngaged): until then, a drag on it moves the window
      if (ImGui::ButtonBehavior (dragRect, dragId, &hovered, &held, ImGuiButtonFlags_PressedOnClick))
      {
        if (SKIV_Viewer_IsImageEngaged ( ))
          s_imageDragActive  = true;
        else
          s_imagePressPending = true;
      }

      s_viewerLayout.imageHovered = hovered;

      if (held && s_imageDragActive)
        ImGui::SetMouseCursor (ImGuiMouseCursor_ResizeAll);
    }
  }

  if (cover.pRawTexSRV.p != nullptr)
  {
    auto& io =
      ImGui::GetIO ();

    bool         changed_zoom = false;
    static float last_zoom    = s_view.zoom;

    static constexpr float ZOOM_MAX = SKIV_ZOOM_MAX;
    static constexpr float ZOOM_MIN = SKIV_ZOOM_MIN;

    // The mouse wheel (and Ctrl+wheel) zoom is handled by SKIV_Viewer_HandleMouseZoomPan, before this frame began

    if (ImGui::IsKeyDown (ImGuiKey_GamepadL2))
    {
      float delta =
        ImGui::GetKeyData (ImGuiKey_GamepadLStickUp  )->AnalogValue >  0.00f ?
        ImGui::GetKeyData (ImGuiKey_GamepadLStickUp  )->AnalogValue *  0.05f :
        ImGui::GetKeyData (ImGuiKey_GamepadLStickDown)->AnalogValue >  0.00f ?
        ImGui::GetKeyData (ImGuiKey_GamepadLStickDown)->AnalogValue * -0.05f : 0.0f;

      if (delta != 0.0f)
      {
        s_view.zoom =
          std::clamp (s_view.zoom + 0.1f * delta, ZOOM_MIN, ZOOM_MAX);
      }
    }

    if (last_zoom != s_view.zoom)
    {
      changed_zoom = true;
      last_zoom    = s_view.zoom;
    }

    if (changed_zoom)
    {
      static int32_t last_toast_id = -1;

      ImGuiToast toast = {
        ImGuiToastType::Info, 500,
          s_view.zoom == 1.0f ? "Zoom Level Reset"
                             : "Zoom Level Changed",
          "New Scale:\t%3.0f%%", s_view.zoom * 100.0f
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
                                      s_view.zoom * 100,
                                     (cover.is_hdr) ? "HDR" : (cover.is_wcg || (cover.anim != nullptr && cover.anim->wide)) ? "SDR, wide gamut" : "SDR",
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

      if (s_view.zoom != 1.0f)
      {
        ImGui::SameLine             ();
        ImGui::BeginGroup           ();
        ImGui::PopFont              ();
        ImGui::Spacing              ();
        ImGui::Spacing              ();
        if (ImGui::Button (ICON_FA_ROTATE_LEFT "###Zoom_Reset"))
          s_view.zoom = 1.0f;
        ImGui::PushFont (fontConsolas, SKIF_ImGui_GetFontConsolasSize ( ));
        ImGui::EndGroup             ();
      }
    }

    // How the file describes its colors; everything it has, on hover
    {
      const std::string colorSpace = SKIV_DescribeColorSource (cover.color_source);

      ImGui::TextUnformatted ("Color Space:");
      ImGui::SameLine        (posXvalues);
      ImGui::TextUnformatted (colorSpace.c_str ());
      SKIF_ImGui_SetHoverTip (SKIV_DescribeColorSourceInDetail (cover.color_source));
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

#pragma region Carousel

  // The thumbnails carousel, over the image and the details: a tile clicked shows its image, as the arrow keys do
  //   (also while one is loading)
  if (const std::wstring clicked = SKIV_Viewer_DrawCarousel (_current_folder.fileList, _store);
      ! clicked.empty ( ) && ! tryingToDownImage)
  {
    _current_folder.fileList.setImage (clicked);
    dragDroppedFilePath = clicked;
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
          bool bEnabled = (s_view.scaling == image_scaling);

          if (bEnabled)
            ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));

          if (ImGui::MenuItem (label, shortcut, (s_view.scaling == image_scaling)))
          {
            s_view.scaling = (s_view.scaling != image_scaling) ? image_scaling : ImageScaling_Auto;
            s_view.zoom    = 1.0f;
          }

          if (bEnabled)
            ImGui::PopStyleColor  ( );
        };

        _CreateMenuItem (ImageScaling_Auto,    "Automatic",        nullptr); // Actual size if smaller than the window, otherwise zoom to fit (the default)
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

    // Update the title of the main app window to indicate we're loading...
    if (SKIF_ImGui_hWnd != NULL)
      ::SetWindowText (SKIF_ImGui_hWnd, L"Loading... - " SKIV_WINDOW_TITLE_SHORT_W);

    // Decoded on a worker thread; taken (shown) at the start of a later frame, see TakeResult above
    _store.Request (new_path, _registry.bAnimatedImages);
    new_path.clear();
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
          // An animated image: its current frame. Wide gamut SDR ones: in their own colors (as scRGB until then).
          const bool wideSDR =
            (! cover.is_hdr && (cover.is_wcg || (cover.anim != nullptr && cover.anim->wide)));

          DirectX::ScratchImage                          captured_img;
          if (SUCCEEDED (SKIV_CaptureImage (cover, captured_img, wideSDR)))
          {
            if (cover.is_hdr)
            {
              hr =
                SKIV_Image_SaveToDisk_HDR (*captured_img.GetImages (), pwszFilePath);
            }

            else if (wideSDR)
            {
              hr =
                SKIV_Image_SaveWideGamutSDR (*captured_img.GetImages (), pwszFilePath, cover.color_source);
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


  // The Left and Right arrow keys switch images: owning them keeps ImGui's keyboard navigation from also scrolling the
  //   viewer with them (as it does in a window without items to navigate to), which would move the image as it is
  //   being swapped out. Not while a popup (e.g. the context menu) or a text field uses them.
  if (! ImGui::GetIO ( ).WantTextInput && ! ImGui::IsPopupOpen (nullptr, ImGuiPopupFlags_AnyPopupId))
  {
    const ImGuiID arrowKeysOwner = ImGui::GetID ("###SKIV_ImageArrowKeys");

    ImGui::SetKeyOwner (ImGuiKey_LeftArrow,  arrowKeysOwner);
    ImGui::SetKeyOwner (ImGuiKey_RightArrow, arrowKeysOwner);
  }

  // Also while an image is loading: going on replaces it (the image store loads the latest one asked for)
  if (! tryingToDownImage)
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

    const std::wstring shown = cover.file_info.path;

    SKIV_ImageStore::Release (cover);
    SKIV_ImageStore::Release (cover_old);

    // Load the image shown again, on the new device (unless one was loading: the store makes that request again)
    if (! _store.OnDeviceReset ( ) && ! shown.empty ( ))
      new_path = shown;
  }


  // XXX: There's probably a better place to put this, I was lazy.
  //
  if (ImGui::GetIO ().KeyCtrl && ImGui::GetKeyData (ImGuiKey_C)->DownDuration == 0.0f)
  {
    wantCopyToClipboard = true;
  }
}
