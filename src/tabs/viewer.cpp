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

#include <stb_image.h>
#include <html_coder.hpp>

#pragma comment (lib, "dxguid.lib")

thread_local stbi__context::cicp_s SKIV_STBI_CICP;

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
float    SKIV_HDR_DisplayMaxLuminance   = 426.0f;
float    SKIV_HDR_BrightnessScale       = 100.0f;
int      SKIV_HDR_TonemapType           = SKIV_HDR_TonemapType::SKIV_TONEMAP_TYPE_MAP_CLL_TO_DISPLAY;

static const DirectX::XMMATRIX c_from709to2020 = // Transposed
{
  { 0.627403914928436279296875f,     0.069097287952899932861328125f,    0.01639143936336040496826171875f, 0.0f },
  { 0.3292830288410186767578125f,    0.9195404052734375f,               0.08801330626010894775390625f,    0.0f },
  { 0.0433130674064159393310546875f, 0.011362315155565738677978515625f, 0.895595252513885498046875f,      0.0f },
  { 0.0f,                            0.0f,                              0.0f,                             1.0f }
};

static const DirectX::XMMATRIX c_from2020toXYZ = // Transposed
{
  { 0.636958062648773193359375f,  0.26270020008087158203125f,      0.0f,                           0.0f },
  { 0.144616901874542236328125f,  0.677998065948486328125f,        0.028072692453861236572265625f, 0.0f },
  { 0.1688809692859649658203125f, 0.0593017153441905975341796875f, 1.060985088348388671875f,       0.0f },
  { 0.0f,                         0.0f,                            0.0f,                           1.0f }
};

static const DirectX::XMMATRIX c_from709toXYZ = // Transposed
{
  { 0.4123907983303070068359375f,  0.2126390039920806884765625f,   0.0193308182060718536376953125f, 0.0f },
  { 0.3575843274593353271484375f,  0.715168654918670654296875f,    0.119194783270359039306640625f,  0.0f },
  { 0.18048079311847686767578125f, 0.072192318737506866455078125f, 0.950532138347625732421875f,     0.0f },
  { 0.0f,                          0.0f,                           0.0f,                            1.0f }
};

static const DirectX::XMMATRIX c_from709toDCIP3 = // Transposed
{
  { 0.82246196269989013671875f,    0.03319419920444488525390625f, 0.017082631587982177734375f,  0.0f },
  { 0.17753803730010986328125f,    0.96680581569671630859375f,    0.0723974406719207763671875f, 0.0f },
  { 0.0f,                          0.0f,                          0.91051995754241943359375f,   0.0f },
  { 0.0f,                          0.0f,                          0.0f,                         1.0f }
};

static const DirectX::XMMATRIX c_from709toAP0 = // Transposed
{
  { 0.4339316189289093017578125f, 0.088618390262126922607421875f, 0.01775003969669342041015625f,  0.0f },
  { 0.3762523829936981201171875f, 0.809275329113006591796875f,    0.109447620809078216552734375f, 0.0f },
  { 0.1898159682750701904296875f, 0.10210628807544708251953125f,  0.872802317142486572265625f,    0.0f },
  { 0.0f,                         0.0f,                           0.0f,                           1.0f }
};

static const DirectX::XMMATRIX c_from709toAP1 = // Transposed
{
  { 0.61702883243560791015625f,       0.333867609500885009765625f,    0.04910354316234588623046875f,     0.0f },
  { 0.069922320544719696044921875f,   0.91734969615936279296875f,     0.012727967463433742523193359375f, 0.0f },
  { 0.02054978720843791961669921875f, 0.107552029192447662353515625f, 0.871898174285888671875f,          0.0f },
  { 0.0f,                             0.0f,                           0.0f,                              1.0f }
};

static const DirectX::XMMATRIX c_fromXYZto709 = // Transposed
{
  {  3.2409698963165283203125f,    -0.96924364566802978515625f,       0.055630080401897430419921875f, 0.0f },
  { -1.53738319873809814453125f,    1.875967502593994140625f,        -0.2039769589900970458984375f,   0.0f },
  { -0.4986107647418975830078125f,  0.0415550582110881805419921875f,  1.05697154998779296875f,        0.0f },
  {  0.0f,                          0.0f,                             0.0f,                           1.0f }
};

static const DirectX::XMMATRIX c_fromXYZtoLMS = // Transposed
{
  {  0.3592, -0.1922, 0.0070, 0.0 },
  {  0.6976,  1.1004, 0.0749, 0.0 },
  { -0.0358,  0.0755, 0.8434, 0.0 },
  {  0.0,     0.0,    0.0,    1.0 }
};

static const DirectX::XMMATRIX c_fromLMStoXYZ = // Transposed
{
  {  2.070180056695613509600,  0.364988250032657479740, -0.049595542238932107896, 0.0 },
  { -1.326456876103021025500,  0.680467362852235141020, -0.049421161186757487412, 0.0 },
  {  0.206616006847855170810, -0.045421753075853231409,  1.187995941732803439400, 0.0 },
  {  0.0,                      0.0,                      0.0,                     1.0 }
};

struct ParamsPQ
{
  DirectX::XMVECTOR N, M;
  DirectX::XMVECTOR C1, C2, C3;
};

static const ParamsPQ PQ =
{
  DirectX::XMVectorReplicate (2610.0 / 4096.0 / 4.0),   // N
  DirectX::XMVectorReplicate (2523.0 / 4096.0 * 128.0), // M
  DirectX::XMVectorReplicate (3424.0 / 4096.0),         // C1
  DirectX::XMVectorReplicate (2413.0 / 4096.0 * 32.0),  // C2
  DirectX::XMVectorReplicate (2392.0 / 4096.0 * 32.0),  // C3
};

auto PQToLinear = [](DirectX::XMVECTOR N, DirectX::XMVECTOR maxPQValue = DirectX::g_XMOne)
{
using namespace DirectX;

  XMVECTOR ret;

  ret =
    XMVectorPow (XMVectorAbs (N), XMVectorDivide (g_XMOne, PQ.M));

  XMVECTOR nd;

  nd =
    XMVectorDivide (
      XMVectorMax (XMVectorSubtract (ret, PQ.C1), g_XMZero),
                   XMVectorSubtract (     PQ.C2,
            XMVectorMultiply (PQ.C3, ret)));

  ret =
    XMVectorMultiply (XMVectorPow (XMVectorAbs (nd), XMVectorDivide (g_XMOne, PQ.N)), maxPQValue);

  return ret;
};

auto LinearToPQ = [](DirectX::XMVECTOR N, DirectX::XMVECTOR maxPQValue = DirectX::g_XMOne)
{
  using namespace DirectX;

  XMVECTOR ret;

  ret =
    XMVectorPow (XMVectorAbs (XMVectorDivide (N, maxPQValue)), PQ.N);

  XMVECTOR nd =
    XMVectorDivide (
       XMVectorAdd (  PQ.C1, XMVectorMultiply (PQ.C2, ret)),
       XMVectorAdd (g_XMOne, XMVectorMultiply (PQ.C3, ret))
    );

  return
    XMVectorPow (XMVectorAbs (nd), PQ.M);
};

float LinearToPQY (float N)
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

auto Rec709toICtCp = [](DirectX::XMVECTOR N)
{
  using namespace DirectX;

  XMVECTOR ret = N;

  ret = XMVector3Transform (ret, c_from709toXYZ);
  ret = XMVector3Transform (ret, c_fromXYZtoLMS);

  ret =
    LinearToPQ (ret, XMVectorReplicate (125.0f));

  static const DirectX::XMMATRIX ConvMat = // Transposed
  {
    { 0.5000,  1.6137,  4.3780, 0.0f },
    { 0.5000, -3.3234, -4.2455, 0.0f },
    { 0.0000,  1.7097, -0.1325, 0.0f },
    { 0.0f,    0.0f,    0.0f,   1.0f }
  };

  return
    XMVector3Transform (ret, ConvMat);
};

auto ICtCptoRec709 = [](DirectX::XMVECTOR N)
{
  using namespace DirectX;

  XMVECTOR ret = N;

  static const DirectX::XMMATRIX ConvMat = // Transposed
  {
    { 1.0,                  1.0,                  1.0,                 0.0f },
    { 0.00860514569398152, -0.00860514569398152,  0.56004885956263900, 0.0f },
    { 0.11103560447547328, -0.11103560447547328, -0.32063747023212210, 0.0f },
    { 0.0f,                 0.0f,                 0.0f,                1.0f }
  };

  ret =
    XMVector3Transform (ret, ConvMat);

  ret = PQToLinear (ret, XMVectorReplicate (125.0f));
  ret = XMVector3Transform (ret, c_fromLMStoXYZ);

  return
    XMVector3Transform (ret, c_fromXYZto709);
};

static const DirectX::XMMATRIX c_scRGBtoBt2100 = // Transposed
{
  { 2939026994.L /  585553224375.L,   76515593.L / 138420033750.L,    12225392.L /   93230009375.L, 0.0 },
  { 9255011753.L / 3513319346250.L, 6109575001.L / 830520202500.L,  1772384008.L / 2517210253125.L, 0.0 },
  {  173911579.L /  501902763750.L,   75493061.L / 830520202500.L, 18035212433.L / 2517210253125.L, 0.0 },
  {                            0.0,                           0.0,                             0.0, 1.0 }
};

static const DirectX::XMMATRIX c_Bt2100toscRGB = // Transposed
{
  {  348196442125.L / 1677558947.L, -579752563250.L / 37238079773.L,  -12183628000.L /  5369968309.L, 0.0f },
  { -123225331250.L / 1677558947.L, 5273377093000.L / 37238079773.L, -472592308000.L / 37589778163.L, 0.0f },
  {  -15276242500.L / 1677558947.L,  -38864558125.L / 37238079773.L, 5256599974375.L / 37589778163.L, 0.0f },
  {                           0.0f,                            0.0f,                            0.0f, 1.0f }
};

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
  FileSignature { L"image/vnd.ms-photo",        { L".jxr"  },          { 0x49, 0x49, 0xBC } },
  FileSignature { L"image/vnd.adobe.photoshop", { L".psd"  },          { 0x38, 0x42, 0x50, 0x53 } },
  FileSignature { L"image/tiff",                { L".tiff", L".tif" }, { 0x49, 0x49, 0x2A, 0x00 } }, // TIFF: little-endian
  FileSignature { L"image/tiff",                { L".tiff", L".tif" }, { 0x4D, 0x4D, 0x00, 0x2A } }, // TIFF: big-endian
  FileSignature { L"image/vnd.radiance",        { L".hdr"  },          { 0x23, 0x3F, 0x52, 0x41, 0x44, 0x49, 0x41, 0x4E, 0x43, 0x45, 0x0A } }, // Radiance High Dynamic Range image file
  FileSignature { L"image/gif",                 { L".gif"  },          { 0x47, 0x49, 0x46, 0x38, 0x37, 0x61 } }, // GIF87a
  FileSignature { L"image/gif",                 { L".gif"  },          { 0x47, 0x49, 0x46, 0x38, 0x39, 0x61 } }, // GIF89a
  FileSignature { L"image/avif",                { L".avif" },          { 0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x61, 0x76, 0x69, 0x66 },   // ftypavif
                                                                       { 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } }, // ?? ?? ?? ?? 66 74 79 70 61 76 69 66
  FileSignature { L"image/vnd-ms.dds",          { L".dds"  },          { 0x44, 0x44, 0x53, 0x20 } },
//FileSignature { L"image/x-targa",             { L".tga"  },          { 0x00, } }, // TGA has no real unique header identifier, so just use the file extension on those
};

const std::initializer_list<FileSignature> supported_sdr_encode_formats =
{
  FileSignature { L"image/png",                 { L".png"  },          { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A } },
  FileSignature { L"image/jpeg",                { L".jpg", L".jpeg" }, { 0xFF, 0xD8, 0x00, 0x00 },   // JPEG (SOI; Start of Image)
                                                                       { 0xFF, 0xFF, 0x00, 0x00 } }, // JPEG App Markers are masked as they can be all over the place (e.g. 0xFF 0xD8 0xFF 0xED)
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
//FileSignature { L"image/avif",                { L".avif" },          { 0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x61, 0x76, 0x69, 0x66 },   // ftypavif
//                                                                     { 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } }, // ?? ?? ?? ?? 66 74 79 70 61 76 69 66
//FileSignature { L"image/vnd.radiance",        { L".hdr"  },          { 0x23, 0x3F, 0x52, 0x41, 0x44, 0x49, 0x41, 0x4E, 0x43, 0x45, 0x0A } }, // Radiance High Dynamic Range image file
//FileSignature { L"image/vnd-ms.dds",          { L".dds"  },          { 0x44, 0x44, 0x53, 0x20 } },
};

bool isExtensionSupported (const std::wstring extension)
{
  for (auto& type : supported_formats)
    if (SKIF_Util_HasFileExtension (extension, type))
      return true;

  return false;
}

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

enum ImageScaling {
  ImageScaling_Auto,
  ImageScaling_None,
  ImageScaling_Fit,
  ImageScaling_Fill,
#ifdef _DEBUG
  ImageScaling_Stretch
#endif
};

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

  float        width       = 0.0f;
  float        height      = 0.0f;
  float        zoom        = 1.0f; // 1.0f = 100%; max: 5.0f; min: 0.05f
  ImageScaling scaling     = ImageScaling_Auto;
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
    width               = 0.0f;
    height              = 0.0f;
    zoom                = 1.0f;
    scaling             = ImageScaling_Auto;
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
  ImageDecoder_DSS,
  ImageDecoder_stbi
};

bool
LoadLibraryTexture (image_s& image)
{
  // NOT REALLY THREAD-SAFE WHILE IT RELIES ON THESE GLOBAL OBJECTS!
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  CComPtr <ID3D11Texture2D> pRawTex2D;
  CComPtr <ID3D11Texture2D> pGamutCoverageTex2D;
  DirectX::TexMetadata        meta = { };
  DirectX::ScratchImage        img = { };

  bool succeeded = false;
  bool converted = false;

  DWORD pre = SKIF_Util_timeGetTime1();

  if (image.file_info.path.empty())
    return false;

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
      return false;
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
             (type.mime_type == L"image/avif"                ) ? ImageDecoder_WIC  :
             (type.mime_type == L"image/vnd-ms.dds"          ) ? ImageDecoder_DSS  :
                                                                 ImageDecoder_WIC;   // Not actually being used

          // None of this is technically correct other than the .hdr case,
          //   they can all be SDR or HDR.
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
  PLOG_DEBUG_IF(decoder == ImageDecoder_DSS ) << "Using DSS decoder...";

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

    SKIV_STBI_CICP = { };

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

    // Fall back to using WIC if STB fails to parse the file
    if (pixels == NULL)
    {
      decoder = ImageDecoder_WIC;
      PLOG_ERROR << "Using WIC decoder due to STB failing with: " << stbi_failure_reason();
    }

    else
    {
      if (SKIV_STBI_CICP.primaries != 0)
      {
        assert (SKIV_STBI_CICP.primaries     ==  9); // BT 2020
        assert (SKIV_STBI_CICP.transfer_func == 16); // ST 2084
        assert (SKIV_STBI_CICP.matrix_coeffs ==  0); // Identity
        // Matrix coeffs. may also presumably be:
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

      if (dxgi_format == DXGI_FORMAT_R32G32B32A32_FLOAT)
      {
        // Good grief this is inefficient, let's convert it to something reasonable...
        DirectX::ScratchImage raw_fp32_img;


        // Check for BT.2020 using ST.2084 (HDR10)
        if ( SKIV_STBI_CICP.primaries     ==  9 &&
             SKIV_STBI_CICP.transfer_func == 16 )
        {
          DirectX::ScratchImage temp_img  = { };
          DirectX::ScratchImage temp_img2 = { };

          meta.format = DXGI_FORMAT_R32G32B32A32_FLOAT;

          if (SUCCEEDED (
              DirectX::LoadFromWICFile (
                image.file_info.path.c_str (),
                  DirectX::WIC_FLAGS_FILTER_POINT | DirectX::WIC_FLAGS_FORCE_LINEAR,
                    &meta, temp_img)))
          {
            PLOG_INFO << "HDR10 PNG detected, transforming to scRGB...";

            if (SUCCEEDED (DirectX::Convert (*temp_img.GetImages (), DXGI_FORMAT_R32G32B32A32_FLOAT, DirectX::TEX_FILTER_DEFAULT, 0.0f, temp_img2)))
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
                    XMVector3Transform (PQToLinear (v), c_Bt2100toscRGB);
                }
              }, img );

              meta.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
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

      stbi_image_free (pixels);
    }
  }

  if (decoder == ImageDecoder_WIC)
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

  if (decoder == ImageDecoder_DSS)
  {
    if (SUCCEEDED (
        DirectX::LoadFromDDSFile (
          image.file_info.path.c_str (),
            DirectX::DDS_FLAGS_PERMISSIVE,
              &meta, img)))
    {
      const DXGI_FORMAT final_format = DXGI_FORMAT_R8G8B8A8_UNORM;
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

        if (FAILED (DirectX::Convert (*img.GetImage (0, 0, 0), final_format, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, temp_img)))
        {
          PLOG_ERROR << "Conversion failed!";
          succeeded = false;
        }

        std::swap (img, temp_img);
        meta = img.GetMetadata ( );
      }

      //meta.format = DirectX::MakeSRGB (DirectX::MakeTypeless (meta.format));
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

  if (! succeeded)
    return false;

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
    return false;

  pRawTex2D        = nullptr;

  succeeded = false;

  if (image.is_hdr)
  {
    using namespace DirectX;

    assert (meta.format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
            meta.format == DXGI_FORMAT_R32G32B32A32_FLOAT);

    XMVECTOR vMaxCLL = g_XMZero;
    XMVECTOR vMaxLum = g_XMZero;
    XMVECTOR vMinLum = g_XMOne;

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

        XMVectorSetZ (v, 1.0f);

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

        vMaxLum =
          XMVectorMax (vMaxLum, vColorXYZ);

        vMinLum =
          XMVectorMin (vMinLum, vColorXYZ);

        dScanlineLum +=
          std::max (0.0, static_cast <double> (XMVectorGetY (v)));

        pixels++;
      }

      dLumAccum +=
        (dScanlineLum / static_cast <float> (width));
    } );

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

    // In XYZ space, so Y=Luminance
    float fMaxLum = XMVectorGetY (vMaxLum);
    float fMinLum = XMVectorGetY (vMinLum);

    if (fMinLum < 0.0f)
    {
      PLOG_INFO  << "HDR image contains invalid (non-Rec2020) colors...";

      fMinLum = 0.0f;
    }

    image.light_info.max_cll      = fMaxCLL;
    image.light_info.max_cll_name = cMaxChannel;
    image.light_info.max_nits     = std::max (0.0f, fMaxLum * 80.0f); // scRGB
    image.light_info.min_nits     = std::max (0.0f, fMinLum * 80.0f); // scRGB

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

  ImVec2 avail_size = ImGui::GetContentRegionAvail ( ) / SKIF_ImGui_GlobalDPIScale;

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

  image.avail_size       = ImVec2(avail_width, avail_height);

  return image.avail_size;
}

#pragma endregion



// Main UI function

void
SKIF_UI_Tab_DrawViewer (void)
{
  extern bool imageFadeActive;
  extern bool wantCopyToClipboard;
  extern ImRect copyRect;

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static DirectX::TexMetadata     meta = { };
  static DirectX::ScratchImage    img  = { };
  static std::wstring new_path = L"";
  static int  tmp_iDarkenImages = _registry.iDarkenImages;
  static image_s cover, cover_old;


  // ** Move this code somewhere more sensible
  //
  // User is requesting to copy the loaded image to clipboard,
  //   let's download it back from the GPU and have some fun!
  if (wantCopyToClipboard && cover.pRawTexSRV.p != nullptr /* && cover.is_hdr */)
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
              extern bool
                  SKIV_Image_CopyToClipboard (const DirectX::Image* pImage, bool snipped);
              if (SKIV_Image_CopyToClipboard (subrect.GetImages (), true))
              {
                std::exchange (wantCopyToClipboard, false);

                copyRect = { 0,0,0,0 };
              }
            }
          }

          else
          {
            extern bool
                SKIV_Image_CopyToClipboard (const DirectX::Image* pImage, bool snipped);
            if (SKIV_Image_CopyToClipboard (captured_img.GetImages (), false))
            {
              std::exchange (wantCopyToClipboard, false);
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
      ImGui::SetCursorPosX ((ImGui::GetContentRegionAvail().x - sizeCover_old.x) * 0.5f);
    if (sizeCover_old.y < ImGui::GetContentRegionAvail().y)
      ImGui::SetCursorPosY ((ImGui::GetContentRegionAvail().y - sizeCover_old.y) * 0.5f);

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

  SKIV_HDR_MaxCLL       = cover.light_info.max_cll;
  SKIV_HDR_MaxLuminance = cover.light_info.max_nits;

  if (sizeCover.x < ImGui::GetContentRegionAvail().x)
    ImGui::SetCursorPosX ((ImGui::GetContentRegionAvail().x - sizeCover.x) * 0.5f);
  if (sizeCover.y < ImGui::GetContentRegionAvail().y)
    ImGui::SetCursorPosY ((ImGui::GetContentRegionAvail().y - sizeCover.y) * 0.5f);

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

    // Using 4.975f and 0.075f to work around some floating point shenanigans
    if (     io.MouseWheel > 0 && cover.zoom < 4.975f && io.KeyCtrl)
      cover.zoom += 0.05f;

    else if (io.MouseWheel < 0 && cover.zoom > 0.075f && io.KeyCtrl)
      cover.zoom -= 0.05f;

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

      // On release, do something
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
                             "%s\n",
                                      cover.width,
                                      cover.height,
                                      cover.zoom * 100,
                                     (cover.is_hdr) ? "HDR" : "SDR");

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

#if 0
      ImGui::TextUnformatted ("Output Format");

      extern bool RecreateSwapChains;
      static int* ptrSDR = nullptr;

      if ((_registry.iHDRMode > 0 && SKIF_Util_IsHDRActive ( )))
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
      if (SKIF_Util_IsHDRActive ())
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

      if (ImGui::Button (ICON_FA_FLOPPY_DISK " Save As..."))
        SaveFileDialog  = PopupState_Open;
      if (ImGui::Button (ICON_FA_FILE_EXPORT "Export to SDR"))
        ExportSDRDialog = PopupState_Open;

      ImGui::Spacing (); ImGui::Spacing (); ImGui::Spacing (); ImGui::Spacing ();
      //ImGui::TextUnformatted ("");

      if (ImGui::Button (ICON_FA_COPY "Copy to Clipboard"))
      {
      }

      static int clipboard_type = 0;
      ImGui::RadioButton ("HDR (.png)", &clipboard_type, 0); ImGui::SameLine ();
      ImGui::RadioButton ("SDR",        &clipboard_type, 1);
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
        // We need to get the luminance capabilities for the current viewport from DXGI
        ImGui_ImplDX11_ViewportData* vd =
          (ImGui_ImplDX11_ViewportData *)ImGui::GetWindowViewport ()->RendererUserData;

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

        if (fCalibrationOverride == 0.0f) SKIV_HDR_DisplayMaxLuminance = vd->HDRLuma;
        else                              SKIV_HDR_DisplayMaxLuminance = bLockCalibration ? abs (fCalibrationOverride)
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
          if ( (SKIV_HDR_BrightnessScale / 100.0f) * SKIV_HDR_MaxLuminance >
                                                     SKIV_HDR_DisplayMaxLuminance )
          {
            ImGui::Spacing (); ImGui::Spacing ();
            ImGui::TextColored (ImColor (0xff0099ff), ICON_FA_TRIANGLE_EXCLAMATION);
            ImGui::SameLine    ();
            ImGui::TextUnformatted ("Content Exceeds Display Capabilities");

            ImGui::RadioButton ("Do Nothing",      &SKIV_HDR_TonemapType, SKIV_TONEMAP_TYPE_NONE);
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

            ImGui::RadioButton ("Clip to Display", &SKIV_HDR_TonemapType, SKIV_TONEMAP_TYPE_CLIP);
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

            ImGui::RadioButton ("Map to Display",  &SKIV_HDR_TonemapType, SKIV_TONEMAP_TYPE_MAP_CLL_TO_DISPLAY);
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
        extern void SKIV_HandleCopyShortcut (void);
                    SKIV_HandleCopyShortcut ();
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

    struct filterspec_s {
      std::list<std::pair<std::wstring, std::wstring>> _raw_list  = { };
      std::vector<COMDLG_FILTERSPEC>                   filterSpec = { };
    };

    auto _CreateFILTERSPEC = [](void) -> filterspec_s
    {
      filterspec_s _spec = { };

      { // All supported formats
        std::wstring ext_filter;

        for (auto& type : supported_formats)
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

      for (auto& type : supported_formats)
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

    static const filterspec_s filters = _CreateFILTERSPEC ( );

#ifdef _DEBUG
    for (auto& filter : filters.filterSpec)
      PLOG_VERBOSE << std::wstring (filter.pszName) << ": " << std::wstring (filter.pszSpec);
#endif

    LPWSTR pwszFilePath = NULL;
    HRESULT hr          = // COMDLG_FILTERSPEC{ L"Images", L"*.png;*.jpg;*.jpeg;*.webp;*.psd;*.bmp;*.jxr;*.hdr;*.avif" }
      SK_FileOpenDialog (&pwszFilePath, filters.filterSpec.data(), static_cast<UINT> (filters.filterSpec.size()), FOS_FILEMUSTEXIST, FOLDERID_Pictures);
          
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

    struct filterspec_s {
      std::list<std::pair<std::wstring, std::wstring>> _raw_list  = { };
      std::vector<COMDLG_FILTERSPEC>                   filterSpec = { };
    };

    auto _CreateFILTERSPEC = [](void) -> filterspec_s
    {
      filterspec_s _spec = { };

      { // All supported formats
        std::wstring ext_filter;

        for (auto& type : cover.is_hdr ? supported_hdr_encode_formats
                                       : supported_sdr_encode_formats)
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

      for (auto& type : cover.is_hdr ? supported_hdr_encode_formats
                                     : supported_sdr_encode_formats)
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

    const filterspec_s filters = _CreateFILTERSPEC ( );

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
      SK_FileSaveDialog (&pwszFilePath, wszCoverName, wszDefaultExtension, filters.filterSpec.data(), static_cast<UINT> (filters.filterSpec.size()), FOS_STRICTFILETYPES|FOS_FILEMUSTEXIST|FOS_OVERWRITEPROMPT|FOS_DONTADDTORECENT, FOLDERID_Pictures);
          
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
            HRESULT SKIF_Image_SaveToDisk_HDR (const DirectX::Image& image, const wchar_t* wszFileName);
            HRESULT SKIF_Image_SaveToDisk_SDR (const DirectX::Image& image, const wchar_t* wszFileName);

            if (cover.is_hdr)
            {
              hr =
                SKIF_Image_SaveToDisk_HDR (*captured_img.GetImages (), pwszFilePath);
            }

            else
            {
              hr =
                SKIF_Image_SaveToDisk_SDR (*captured_img.GetImages (), pwszFilePath);
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

    struct filterspec_s {
      std::list<std::pair<std::wstring, std::wstring>> _raw_list  = { };
      std::vector<COMDLG_FILTERSPEC>                   filterSpec = { };
    };

    auto _CreateFILTERSPEC = [](void) -> filterspec_s
    {
      filterspec_s _spec = { };

      { // All supported formats
        std::wstring ext_filter;

        for (auto& type : supported_sdr_encode_formats)
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

      for (auto& type : supported_sdr_encode_formats)
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

    const filterspec_s filters = _CreateFILTERSPEC ( );

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
      SK_FileSaveDialog (&pwszFilePath, wszCoverName, wszDefaultExtension, filters.filterSpec.data(), static_cast<UINT> (filters.filterSpec.size()), FOS_STRICTFILETYPES|FOS_FILEMUSTEXIST, FOLDERID_Pictures);
          
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
            HRESULT SKIF_Image_SaveToDisk_HDR (const DirectX::Image& image, const wchar_t* wszFileName);
            HRESULT SKIF_Image_SaveToDisk_SDR (const DirectX::Image& image, const wchar_t* wszFileName);

            if (cover.is_hdr)
            {
              hr =
                SKIF_Image_SaveToDisk_SDR (*captured_img.GetImages (), pwszFilePath);
            }

            else
            {
              // WTF? It's already SDR... oh well, save it anyway
              hr =
                SKIF_Image_SaveToDisk_SDR (*captured_img.GetImages (), pwszFilePath);
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
    extern void SKIV_HandleCopyShortcut (void);
                SKIV_HandleCopyShortcut ();
  }
}



















uint32_t
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

struct SK_PNG_HDR_cHRM_Payload
{
  uint32_t white_x = 31270;
  uint32_t white_y = 32900;
  uint32_t red_x   = 70800;
  uint32_t red_y   = 29200;
  uint32_t green_x = 17000;
  uint32_t green_y = 79700;
  uint32_t blue_x  = 13100;
  uint32_t blue_y  = 04600;
};

struct SK_PNG_HDR_sBIT_Payload
{
  uint8_t red_bits   = 10; // 12 if source was scRGB (compression optimization)
  uint8_t green_bits = 10; // 12 if source was scRGB (compression optimization)
  uint8_t blue_bits  = 10; // 12 if source was scRGB (compression optimization)
  uint8_t alpha_bits =  1; // Spec says it must be > 0... :shrug:
};

struct SK_PNG_HDR_mDCv_Payload
{
  struct {
    uint32_t red_x   = 35400; // 0.708 / 0.00002
    uint32_t red_y   = 14600; // 0.292 / 0.00002
    uint32_t green_x =  8500; // 0.17  / 0.00002
    uint32_t green_y = 39850; // 0.797 / 0.00002
    uint32_t blue_x  =  6550; // 0.131 / 0.00002
    uint32_t blue_y  =  2300; // 0.046 / 0.00002
  } primaries;

  struct {
    uint32_t x       = 15635; // 0.3127 / 0.00002
    uint32_t y       = 16450; // 0.3290 / 0.00002
  } white_point;

  // The only real data we need to fill-in
  struct {
    uint32_t maximum = 10000000; // 1000.0 cd/m^2
    uint32_t minimum = 1;        // 0.0001 cd/m^2
  } luminance;
};

struct SK_PNG_HDR_cLLi_Payload
{
  uint32_t max_cll  = 10000000; // 1000 / 0.0001
  uint32_t max_fall =  2500000; //  250 / 0.0001
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

bool
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

SK_PNG_HDR_cLLi_Payload
SKIV_HDR_CalculateContentLightInfo (const DirectX::Image& img)
{
  using namespace DirectX;

  SK_PNG_HDR_cLLi_Payload clli;

  float N         = 0.0f;
  float fLumAccum = 0.0f;
  XMVECTOR vMaxLum = g_XMZero;

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
                PQToLinear (XMVectorSaturate (v)), c_from2020toXYZ
              );

            vMaxLum =
              XMVectorMax (vMaxLum, v);

            fScanlineLum += XMVectorGetY (v);
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

            vMaxLum =
              XMVectorMax (vMaxLum, v);

            fScanlineLum += XMVectorGetY (v);
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
    clli.max_cll  =
      static_cast <uint32_t> (round ((80.0f * XMVectorGetY (vMaxLum)) / 0.0001f));
    clli.max_fall = 
      static_cast <uint32_t> (round ((80.0f * (fLumAccum / N))        / 0.0001f));
  }

  return clli;
}

bool
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
          XMVECTOR nvalue = XMVector3Transform (v, c_scRGBtoBt2100);
                        v = LinearToPQ (nvalue);
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

bool
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
        static_cast <unsigned char> (DirectX::BitsPerColor (raw_img.format)), 1
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

      mdcv_data.luminance.minimum =
        static_cast <uint32_t> (round (active_display.gamut.minY / 0.0001f));
      mdcv_data.luminance.maximum =
        static_cast <uint32_t> (round (active_display.gamut.maxY / 0.0001f));

      mdcv_data.primaries.red_x =
        static_cast <uint32_t> (round (active_display.gamut.xr / 0.00002));
      mdcv_data.primaries.red_y =
        static_cast <uint32_t> (round (active_display.gamut.yr / 0.00002));

      mdcv_data.primaries.green_x =
        static_cast <uint32_t> (round (active_display.gamut.xg / 0.00002));
      mdcv_data.primaries.green_y =
        static_cast <uint32_t> (round (active_display.gamut.yg / 0.00002));

      mdcv_data.primaries.blue_x =
        static_cast <uint32_t> (round (active_display.gamut.xb / 0.00002));
      mdcv_data.primaries.blue_y =
        static_cast <uint32_t> (round (active_display.gamut.yb / 0.00002));

      mdcv_data.white_point.x =
        static_cast <uint32_t> (round (active_display.gamut.Xw / 0.00002));
      mdcv_data.white_point.y =
        static_cast <uint32_t> (round (active_display.gamut.Yw / 0.00002));
#endif

      SK_PNG_Chunk iccp_chunk = { sizeof (SK_PNG_HDR_iCCP_Payload), { 'i','C','C','P' }, &iccp_data };
      SK_PNG_Chunk cicp_chunk = { sizeof (cicp_data),               { 'c','I','C','P' }, &cicp_data };
      SK_PNG_Chunk clli_chunk = { sizeof (clli_data),               { 'c','L','L','i' }, &clli_data };
      SK_PNG_Chunk sbit_chunk = { sizeof (sbit_data),               { 's','B','I','T' }, &sbit_data };
      SK_PNG_Chunk chrm_chunk = { sizeof (chrm_data),               { 'c','H','R','M' }, &chrm_data };
#if 0
      SK_PNG_Chunk mdcv_chunk = { sizeof (mdcv_data),               { 'm','D','C','v' }, &mdcv_data };
#endif

      iccp_chunk.write (fPNG);
      cicp_chunk.write (fPNG);
      clli_chunk.write (fPNG);
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

#if 0
      SK_LOGi1 (L"Applied HDR10 PNG chunks to %ws.", wszFilePath);
      SK_LOGi1 (L" >> MaxCLL: %.6f nits, MaxFALL: %.6f nits",
                static_cast <double> (clli_data.max_cll)  * 0.0001,
                static_cast <double> (clli_data.max_fall) * 0.0001);
      SK_LOGi1 (L" >> Mastering Display Min/Max Luminance: %.6f/%.6f nits",
                static_cast <double> (mdcv_data.luminance.minimum) * 0.0001,
                static_cast <double> (mdcv_data.luminance.maximum) * 0.0001);
#endif

      return true;
    }
  }

  return false;
}

void
SK_WIC_SetMaximumQuality (IPropertyBag2 *props)
{
  if (props == nullptr)
    return;

  PROPBAG2 opt = {   .pstrName = L"ImageQuality"   };
  VARIANT  var = { VT_R4,0,0,0, { .fltVal = 1.0f } };

  PROPBAG2 opt2 = { .pstrName = L"FilterOption"                    };
  VARIANT  var2 = { VT_UI1,0,0,0, { .bVal = WICPngFilterAdaptive } };

  props->Write (1, &opt,  &var);
  props->Write (1, &opt2, &var2);
}

bool
SKIV_HDR_SavePNGToDisk (const wchar_t* wszPNGPath, const DirectX::Image* png_image,
                                                   const DirectX::Image* raw_image,
                           const char* szUtf8MetadataTitle)
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

    return (png_image->format == DXGI_FORMAT_R16G16B16A16_UNORM)
           ? SKIV_PNG_MakeHDR (wszPNGPath, *png_image, *raw_image)
           : true;
  } else
    PLOG_VERBOSE << "DirectX::SaveToWICFile ( ): FAILED";

  return false;
}

// The parameters are screwy here because currently the only successful way
//   of doing this copy involves passing the path to a file, but the intention
//     is actually to pass raw image data and transfer it using OLE.
bool
SKIV_PNG_CopyToClipboard (const DirectX::Image& image, const void *pData, size_t data_size)
{
  std::ignore = image;
  std::ignore = data_size; // It's a string, we can compute the size trivially

  if (OpenClipboard (nullptr))
  {
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

    GlobalUnlock     (hdrop);
    EmptyClipboard   ();
    SetClipboardData (CF_HDROP, hdrop);
    CloseClipboard   ();

    return true;
  }

  return false;
}


ImRect copyRect = { 0,0,0,0 };
bool wantCopyToClipboard = false;
void SKIV_HandleCopyShortcut (void)
{
  wantCopyToClipboard = true;
}

CComPtr <ID3D11ShaderResourceView> SKIV_DesktopImage;

bool SKIV_Image_CopyToClipboard (const DirectX::Image* pImage, bool snipped)
{
  if (pImage == nullptr)
    return false;

  DirectX::ScratchImage                    hdr10_img;
  if (SKIV_HDR_ConvertImageToPNG (*pImage, hdr10_img))
  {
    PLOG_VERBOSE << "SKIV_HDR_ConvertImageToPNG ( ): TRUE";

    wchar_t                         wszPNGPath [MAX_PATH + 2] = { };
    GetCurrentDirectoryW (MAX_PATH, wszPNGPath);

    PathAppendW       (wszPNGPath, snipped ? L"SKIV_HDR_Snip"
                                           : L"SKIV_HDR_Clipboard");
    PathAddExtensionW (wszPNGPath, L".png");

    if (SKIV_HDR_SavePNGToDisk (wszPNGPath, hdr10_img.GetImages (), pImage, nullptr))
    {
      PLOG_VERBOSE << "SKIV_HDR_SavePNGToDisk ( ): TRUE";

      if (SKIV_PNG_CopyToClipboard (*hdr10_img.GetImage (0,0,0), wszPNGPath, 0))
      {
        PLOG_VERBOSE << "SKIV_PNG_CopyToClipboard ( ): TRUE";
        return true;
      }
    }
  }

  return false;
}

#include <dxgi1_5.h>

HRESULT
SKIV_Image_CaptureDesktop (DirectX::ScratchImage& image, int flags = 0x0)
{
  SKIV_DesktopImage = nullptr;

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

  POINT          cursor_pos;
  GetCursorPos (&cursor_pos);

  CComPtr <IDXGIOutput> pCursorOutput;

  while (SUCCEEDED (pFactory->EnumAdapters (uiAdapter++, &pAdapter.p)))
  {
    CComPtr <IDXGIOutput> pOutput;
    UINT                 uiOutput = 0;

    while (SUCCEEDED (pAdapter->EnumOutputs (uiOutput++, &pOutput)))
    {
      DXGI_OUTPUT_DESC   out_desc;
      pOutput->GetDesc (&out_desc);

      if (out_desc.AttachedToDesktop && PtInRect (&out_desc.DesktopCoordinates, cursor_pos))
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
  {
    return E_UNEXPECTED;
  }

  CComQIPtr <IDXGIOutput5> pOutput5 (pCursorOutput);

  // Down-level interfaces support duplication, but for HDR we want Output5
  if (! pOutput5)
  {
    return E_NOTIMPL;
  }

  DXGI_FORMAT capture_formats [5] = {
    DXGI_FORMAT_R8G8B8A8_UNORM, // Not HDR...
    DXGI_FORMAT_B8G8R8X8_UNORM, // Not HDR...
    DXGI_FORMAT_R10G10B10A2_UNORM,
    DXGI_FORMAT_R16G16B16A16_FLOAT,
    DXGI_FORMAT_R32G32B32A32_FLOAT
  };

  CComPtr <IDXGIOutputDuplication> pDuplicator;
  pOutput5->DuplicateOutput1 (pDevice, 0x0, _ARRAYSIZE (capture_formats),
                                                        capture_formats, &pDuplicator.p);

  if (! pDuplicator)
  {
    return E_NOTIMPL;
  }

  DXGI_OUTDUPL_FRAME_INFO frame_info = { };
  CComPtr <IDXGIResource> pDuplicatedResource;

  int    tries = 0;
  while (tries++ < 3)
  {
    pDuplicator->AcquireNextFrame (150, &frame_info, &pDuplicatedResource.p);

    if (frame_info.LastPresentTime.QuadPart)
      break;

    pDuplicator->ReleaseFrame ();
  }

  if (! pDuplicatedResource)
  {
    return E_UNEXPECTED;
  }

  CComQIPtr <IDXGISurface>    pSurface       (pDuplicatedResource);
  CComQIPtr <ID3D11Texture2D> pDuplicatedTex (pSurface);

  if (! pDuplicatedTex)
  {
    return E_NOTIMPL;
  }

  DXGI_SURFACE_DESC   surfDesc;
  pSurface->GetDesc (&surfDesc);

  CComPtr <ID3D11Texture2D> pStagingTex;
  CComPtr <ID3D11Texture2D> pDesktopImage; // For rendering during snipping

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

#if 0
  if (FAILED (pDevice->CreateTexture2D (&texDesc, nullptr, &pStagingTex.p)))
  {
    pDuplicator->ReleaseFrame ();
    return E_UNEXPECTED;
  }

  pDevCtx->CopyResource (pStagingTex,   pDuplicatedTex);

  D3D11_MAPPED_SUBRESOURCE mapped;

  if (SUCCEEDED (pDevCtx->Map (pStagingTex, 0, D3D11_MAP_READ, 0x0, &mapped)))
  {
    image.Initialize2D (surfDesc.Format,
                        surfDesc.Width,
                        surfDesc.Height, 1, 1
    );

    if (! image.GetPixels ())
    {
      pSurface->Unmap ();
      return E_POINTER;
    }

    auto pImg =
      image.GetImages ();

    const uint8_t* src = (const uint8_t *)mapped.pData;
          uint8_t* dst = pImg->pixels;

    for (size_t h = 0; h < surfDesc.Height; ++h)
    {
      size_t msize =
        std::min <size_t> (pImg->rowPitch, mapped.RowPitch);

      memcpy_s (dst, pImg->rowPitch, src, msize);

      src += mapped.RowPitch;
      dst += pImg->rowPitch;
    }

    if (FAILED (pSurface->Unmap ()))
    {
      return E_UNEXPECTED;
    }
  }
#else
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
  pDevice->CreateShaderResourceView (pDesktopImage, &srvDesc, &SKIV_DesktopImage);

  pDevCtx->Flush ();

  pDuplicator->ReleaseFrame ();
#endif

  return S_OK;
}

HRESULT
SKIF_Image_SaveToDisk_SDR (const DirectX::Image& image, const wchar_t* wszFileName)
{
  using namespace DirectX;

  const Image* pOutputImage = &image;

  XMVECTOR maxLum = XMVectorZero          (),
           minLum = XMVectorSplatInfinity ();

  double lumTotal    = 0.0;
  double logLumTotal = 0.0;
  double N           = 0.0;

  bool is_hdr = false;

  ScratchImage scrgb;
  ScratchImage final_sdr;

  if (image.format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
      image.format == DXGI_FORMAT_R32G32B32A32_FLOAT)
  {
    is_hdr = true;

    if (FAILED (scrgb.InitializeFromImage (image)))
      return E_INVALIDARG;
  }

  if (is_hdr)
  {
    ScratchImage tonemapped_hdr;
    ScratchImage tonemapped_copy;

    EvaluateImage ( scrgb.GetImages     (),
                    scrgb.GetImageCount (),
                    scrgb.GetMetadata   (),
    [&](const XMVECTOR* pixels, size_t width, size_t y)
    {
      UNREFERENCED_PARAMETER(y);

      for (size_t j = 0; j < width; ++j)
      {
        XMVECTOR v = *pixels;

        v =
          XMVector3Transform (v, c_from709toXYZ);

        maxLum =
          XMVectorReplicate (XMVectorGetY (XMVectorMax (v, maxLum)));

        minLum =
          XMVectorReplicate (XMVectorGetY (XMVectorMin (v, minLum)));

        logLumTotal +=
          log2 ( std::max (0.000001, static_cast <double> (std::max (0.0f, XMVectorGetY (v)))) );
           lumTotal +=               static_cast <double> (std::max (0.0f, XMVectorGetY (v)));
        ++N;

        v = XMVectorMax (g_XMZero, v);
  
        pixels++;
      }
    });

    //SK_LOGi0 ( L"Min Luminance: %f, Max Luminance: %f", std::max (0.0f, XMVectorGetY (minLum)) * 80.0f,
    //                                                                    XMVectorGetY (maxLum)  * 80.0f );
    //
    //SK_LOGi0 ( L"Mean Luminance (arithmetic, geometric): %f, %f", 80.0 *      ( lumTotal    / N ),
    //                                                              80.0 * exp2 ( logLumTotal / N ) );

    // After tonemapping, re-normalize the image to preserve peak white,
    //   this is important in cases where the maximum luminance was < 1000 nits
    XMVECTOR maxTonemappedRGB = g_XMZero;

    // If it's too bright, don't bother trying to tonemap the full range...
    static constexpr float _maxNitsToTonemap = 10000.0f/80.0f;

    const float maxYInPQ =
      LinearToPQY (std::min (_maxNitsToTonemap, XMVectorGetY (maxLum))),
               SDR_YInPQ =
      LinearToPQY (                                              1.25f);

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

        static const XMVECTOR vLumaRescale =
          XMVectorReplicate (1.0/1.6f);

        for (size_t j = 0; j < width; ++j)
        {
          XMVECTOR value = inPixels [j];

          value =
            XMVectorMultiply (value, vLumaRescale);

          XMVECTOR ICtCp =
            Rec709toICtCp (value);

          float Y_in  = std::max (XMVectorGetX (ICtCp), 0.0f);
          float Y_out = 1.0f;

          Y_out =
            TonemapHDR (Y_in, maxYInPQ, SDR_YInPQ);

          if (Y_out + Y_in > 0.0f)
          {
            ICtCp.m128_f32 [0] *=
              std::max ((Y_out / Y_in), 0.0f);
          }

          value =
            ICtCptoRec709 (ICtCp);

          maxTonemappedRGB =
            XMVectorMax (maxTonemappedRGB, XMVectorMax (value, g_XMZero));

          outPixels [j] = XMVectorSaturate (value);
        }
      }, tonemapped_hdr
    );

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

    if (FAILED (DirectX::Convert (*tonemapped_hdr.GetImages (), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                                  (TEX_FILTER_FLAGS)0x200000FF, 1.0f, final_sdr)))
    {
      return E_UNEXPECTED;
    }

    pOutputImage =
      final_sdr.GetImages ();
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

  GUID      wic_codec;
  WIC_FLAGS wic_flags = WIC_FLAGS_DITHER_DIFFUSION;

  if (StrStrIW (wszExtension, L"jpg") ||
      StrStrIW (wszExtension, L"jpeg"))
  {
    wic_codec = GetWICCodec (WIC_CODEC_JPEG);
  }

  else if (StrStrIW (wszExtension, L"png"))
  {
    wic_codec = GetWICCodec (WIC_CODEC_PNG);
  }

  else if (StrStrIW (wszExtension, L"bmp"))
  {
    wic_codec = GetWICCodec (WIC_CODEC_BMP);
  }

  else if (StrStrIW (wszExtension, L"tiff"))
  {
    wic_codec = GetWICCodec (WIC_CODEC_TIFF);
  }

  // Probably ignore this
  else if (StrStrIW (wszExtension, L"hdp"))
  {
    wic_codec = GetWICCodec (WIC_CODEC_WMP);
  }

  // AVIF technically works for SDR... do we want to support it?
  //  If we do, WIC won't help us, however.

  else
  {
    return E_UNEXPECTED;
  }

  ///DirectX::TexMetadata              orig_tex_metadata;
  ///CComPtr <IWICMetadataQueryReader> pQueryReader;
  ///
  ///DirectX::GetMetadataFromWICFile (wszOriginalFile, DirectX::WIC_FLAGS_NONE, orig_tex_metadata, [&](IWICMetadataQueryReader *pMQR){
  ///  pQueryReader = pMQR;
  ///});

  return
    DirectX::SaveToWICFile (*pOutputImage, wic_flags, wic_codec,
                      wszImplicitFileName, nullptr, SK_WIC_SetMaximumQuality);
}

HRESULT
SKIF_Image_SaveToDisk_HDR (const DirectX::Image& image, const wchar_t* wszFileName)
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
    PathAddExtension (wszImplicitFileName, defaultHDRFileExt.c_str());
    wszExtension =
      PathFindExtensionW (wszImplicitFileName);
  }

  GUID wic_codec;

  if (StrStrIW (wszExtension, L"jxr"))
  {
    wic_codec = GetWICCodec (WIC_CODEC_WMP);
  }

  else if (StrStrIW (wszExtension, L"png"))
  {
    DirectX::ScratchImage                  png_img;
    if (SKIV_HDR_ConvertImageToPNG (image, png_img))
    {
      if (SKIV_HDR_SavePNGToDisk (wszImplicitFileName, png_img.GetImages (), &image, nullptr))
      {
        return S_OK;
      }
    }
  }

  else if (StrStrIW (wszExtension, L"avif") ||
           StrStrIW (wszExtension, L"hdr")  ||
           StrStrIW (wszExtension, L"jxl"))
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