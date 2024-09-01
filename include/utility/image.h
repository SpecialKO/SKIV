#pragma once

#include <DirectXTex.h>
#include <imgui/imgui_internal.h>
#include <ImGuiNotify.hpp>
#include <atlbase.h>

#pragma warning( push )
#pragma warning( disable : 4305 )

constexpr DirectX::XMMATRIX c_from709to2020 = // Transposed
{
  { 0.627403914928436279296875f,     0.069097287952899932861328125f,    0.01639143936336040496826171875f, 0.0f },
  { 0.3292830288410186767578125f,    0.9195404052734375f,               0.08801330626010894775390625f,    0.0f },
  { 0.0433130674064159393310546875f, 0.011362315155565738677978515625f, 0.895595252513885498046875f,      0.0f },
  { 0.0f,                            0.0f,                              0.0f,                             1.0f }
};

constexpr DirectX::XMMATRIX c_from2020toXYZ = // Transposed
{
  { 0.636958062648773193359375f,  0.26270020008087158203125f,      0.0f,                           0.0f },
  { 0.144616901874542236328125f,  0.677998065948486328125f,        0.028072692453861236572265625f, 0.0f },
  { 0.1688809692859649658203125f, 0.0593017153441905975341796875f, 1.060985088348388671875f,       0.0f },
  { 0.0f,                         0.0f,                            0.0f,                           1.0f }
};

constexpr DirectX::XMMATRIX c_from709toXYZ = // Transposed
{
  { 0.4123907983303070068359375f,  0.2126390039920806884765625f,   0.0193308182060718536376953125f, 0.0f },
  { 0.3575843274593353271484375f,  0.715168654918670654296875f,    0.119194783270359039306640625f,  0.0f },
  { 0.18048079311847686767578125f, 0.072192318737506866455078125f, 0.950532138347625732421875f,     0.0f },
  { 0.0f,                          0.0f,                           0.0f,                            1.0f }
};

constexpr DirectX::XMMATRIX c_from709toDCIP3 = // Transposed
{
  { 0.82246196269989013671875f,    0.03319419920444488525390625f, 0.017082631587982177734375f,  0.0f },
  { 0.17753803730010986328125f,    0.96680581569671630859375f,    0.0723974406719207763671875f, 0.0f },
  { 0.0f,                          0.0f,                          0.91051995754241943359375f,   0.0f },
  { 0.0f,                          0.0f,                          0.0f,                         1.0f }
};

constexpr DirectX::XMMATRIX c_from709toAP0 = // Transposed
{
  { 0.4339316189289093017578125f, 0.088618390262126922607421875f, 0.01775003969669342041015625f,  0.0f },
  { 0.3762523829936981201171875f, 0.809275329113006591796875f,    0.109447620809078216552734375f, 0.0f },
  { 0.1898159682750701904296875f, 0.10210628807544708251953125f,  0.872802317142486572265625f,    0.0f },
  { 0.0f,                         0.0f,                           0.0f,                           1.0f }
};

constexpr DirectX::XMMATRIX c_from709toAP1 = // Transposed
{
  { 0.61702883243560791015625f,       0.333867609500885009765625f,    0.04910354316234588623046875f,     0.0f },
  { 0.069922320544719696044921875f,   0.91734969615936279296875f,     0.012727967463433742523193359375f, 0.0f },
  { 0.02054978720843791961669921875f, 0.107552029192447662353515625f, 0.871898174285888671875f,          0.0f },
  { 0.0f,                             0.0f,                           0.0f,                              1.0f }
};

constexpr DirectX::XMMATRIX c_fromXYZto709 = // Transposed
{
  {  3.2409698963165283203125f,    -0.96924364566802978515625f,       0.055630080401897430419921875f, 0.0f },
  { -1.53738319873809814453125f,    1.875967502593994140625f,        -0.2039769589900970458984375f,   0.0f },
  { -0.4986107647418975830078125f,  0.0415550582110881805419921875f,  1.05697154998779296875f,        0.0f },
  {  0.0f,                          0.0f,                             0.0f,                           1.0f }
};

constexpr DirectX::XMMATRIX c_fromXYZtoLMS = // Transposed
{
  {  0.3592f, -0.1922f, 0.0070f, 0.0f },
  {  0.6976f,  1.1004f, 0.0749f, 0.0f },
  { -0.0358f,  0.0755f, 0.8434f, 0.0f },
  {  0.0f,     0.0f,    0.0f,    1.0f }
};

constexpr DirectX::XMMATRIX c_fromLMStoXYZ = // Transposed
{
  {  2.070180056695613509600f,  0.364988250032657479740f, -0.049595542238932107896f, 0.0f },
  { -1.326456876103021025500f,  0.680467362852235141020f, -0.049421161186757487412f, 0.0f },
  {  0.206616006847855170810f, -0.045421753075853231409f,  1.187995941732803439400f, 0.0f },
  {  0.0f,                      0.0f,                      0.0f,                     1.0f }
};

constexpr DirectX::XMMATRIX c_scRGBtoBt2100 = // Transposed
{
  { 2939026994.0f /  585553224375.0f,   76515593.0f / 138420033750.0f,    12225392.0f /   93230009375.0f, 0.0f },
  { 9255011753.0f / 3513319346250.0f, 6109575001.0f / 830520202500.0f,  1772384008.0f / 2517210253125.0f, 0.0f },
  {  173911579.0f /  501902763750.0f,   75493061.0f / 830520202500.0f, 18035212433.0f / 2517210253125.0f, 0.0f },
  {                             0.0f,                            0.0f,                              0.0f, 1.0f }
};

constexpr DirectX::XMMATRIX c_Bt2100toscRGB = // Transposed
{
  {  348196442125.0f / 1677558947.0f, -579752563250.0f / 37238079773.0f,  -12183628000.0f /  5369968309.0f, 0.0f },
  { -123225331250.0f / 1677558947.0f, 5273377093000.0f / 37238079773.0f, -472592308000.0f / 37589778163.0f, 0.0f },
  {  -15276242500.0f / 1677558947.0f,  -38864558125.0f / 37238079773.0f, 5256599974375.0f / 37589778163.0f, 0.0f },
  {                             0.0f,                              0.0f,                              0.0f, 1.0f }
};

struct ParamsPQ
{
  DirectX::XMVECTOR N,       M;
  DirectX::XMVECTOR C1, C2, C3;
  DirectX::XMVECTOR MaxPQ;
  DirectX::XMVECTOR RcpN, RcpM;
};

static const ParamsPQ PQ =
{
  DirectX::XMVectorReplicate  (2610.0 / 4096.0 / 4.0),   // N
  DirectX::XMVectorReplicate  (2523.0 / 4096.0 * 128.0), // M
  DirectX::XMVectorReplicate  (3424.0 / 4096.0),         // C1
  DirectX::XMVectorReplicate  (2413.0 / 4096.0 * 32.0),  // C2
  DirectX::XMVectorReplicate  (2392.0 / 4096.0 * 32.0),  // C3
  DirectX::XMVectorReplicate  (125.0f),                  // MaxPQ
  DirectX::XMVectorReciprocal (DirectX::XMVectorReplicate (2610.0 / 4096.0 / 4.0)),
  DirectX::XMVectorReciprocal (DirectX::XMVectorReplicate (2523.0 / 4096.0 * 128.0)),
};

#pragma warning( pop )

// Declarations
DirectX::XMVECTOR SKIV_Image_PQToLinear    (DirectX::XMVECTOR N, DirectX::XMVECTOR maxPQValue = DirectX::g_XMOne);
DirectX::XMVECTOR SKIV_Image_LinearToPQ    (DirectX::XMVECTOR N, DirectX::XMVECTOR maxPQValue = DirectX::g_XMOne);
float             SKIV_Image_LinearToPQY   (float N);
DirectX::XMVECTOR SKIV_Image_Rec709toICtCp (DirectX::XMVECTOR N);
DirectX::XMVECTOR SKIV_Image_ICtCptoRec709 (DirectX::XMVECTOR N);

bool    SKIV_Image_CopyToClipboard (const DirectX::Image* pImage, bool snipped, bool isHDR);
HRESULT SKIV_Image_SaveToDisk_HDR  (const DirectX::Image& image, const wchar_t* wszFileName);
HRESULT SKIV_Image_SaveToDisk_SDR  (const DirectX::Image& image, const wchar_t* wszFileName, bool force_sRGB);
HRESULT SKIV_Image_CaptureDesktop  (DirectX::ScratchImage& image, POINT pos, int flags = 0x0);
void    SKIV_Image_CaptureRegion   (ImRect capture_area);
HRESULT SKIV_Image_TonemapToSDR    (const DirectX::Image& image, DirectX::ScratchImage& final_sdr, float mastering_max_nits, float mastering_sdr_nits);

bool    SKIV_Image_IsUltraHDR      (const wchar_t* wszFileName);
bool    SKIV_Image_IsUltraHDR      (void* data, int size);
HRESULT SKIV_Image_LoadUltraHDR    (DirectX::ScratchImage& image, void* data, int size);

#include <avif/avif.h>

bool isAVIFEncoderAvailable (void);

using avifEncoderCreate_pfn          = avifEncoder*(*)(void);
using avifEncoderDestroy_pfn         = void        (*)(avifEncoder*  encoder);
using avifEncoderAddImage_pfn        = avifResult  (*)(avifEncoder*  encoder, const avifImage* image, uint64_t durationInTimescales, avifAddImageFlags addImageFlags);
using avifEncoderFinish_pfn          = avifResult  (*)(avifEncoder*  encoder, avifRWData* output);

using avifDecoderCreate_pfn          = avifDecoder*(*)(void);
using avifDecoderDestroy_pfn         = void        (*)(avifDecoder*  decoder);

using avifDecoderSetIOMemory_pfn     = avifResult  (*)(avifDecoder*  decoder, const uint8_t* data, size_t size);
using avifDecoderParse_pfn           = avifResult  (*)(avifDecoder*  decoder);
using avifDecoderNextImage_pfn       = avifResult  (*)(avifDecoder*  decoder);
using avifDecoderRead_pfn            = avifResult  (*)(avifDecoder*  decoder, avifImage* image);
using avifDecoderReadMemory_pfn      = avifResult  (*)(avifDecoder*  decoder, avifImage* image, const uint8_t* data, size_t size);

using avifImageCreate_pfn            = avifImage*  (*)(uint32_t width, uint32_t height, uint32_t depth, avifPixelFormat yuvFormat);
using avifImageRGBToYUV_pfn          = avifResult  (*)(      avifImage* image, const avifRGBImage* rgb);
using avifImageYUVToRGB_pfn          = avifResult  (*)(const avifImage* image,       avifRGBImage* rgb);
using avifImageDestroy_pfn           = void        (*)(      avifImage* image);

using avifRGBImageAllocatePixels_pfn = avifResult  (*)(avifRGBImage* rgb);
using avifRGBImageFreePixels_pfn     = void        (*)(avifRGBImage* rgb);
using avifRGBImageSetDefaults_pfn    = void        (*)(avifRGBImage* rgb, const avifImage* image);

extern avifEncoderCreate_pfn          SK_avifEncoderCreate;
extern avifEncoderDestroy_pfn         SK_avifEncoderDestroy;
extern avifEncoderAddImage_pfn        SK_avifEncoderAddImage;
extern avifEncoderFinish_pfn          SK_avifEncoderFinish;

extern avifDecoderCreate_pfn          SK_avifDecoderCreate;
extern avifDecoderDestroy_pfn         SK_avifDecoderDestroy;

extern avifDecoderSetIOMemory_pfn     SK_avifDecoderSetIOMemory;
extern avifDecoderParse_pfn           SK_avifDecoderParse;
extern avifDecoderNextImage_pfn       SK_avifDecoderNextImage;
extern avifDecoderRead_pfn            SK_avifDecoderRead;
extern avifDecoderReadMemory_pfn      SK_avifDecoderReadMemory;

extern avifImageCreate_pfn            SK_avifImageCreate;
extern avifImageRGBToYUV_pfn          SK_avifImageRGBToYUV;
extern avifImageYUVToRGB_pfn          SK_avifImageYUVToRGB;
extern avifImageDestroy_pfn           SK_avifImageDestroy;

extern avifRGBImageSetDefaults_pfn    SK_avifRGBImageSetDefaults;
extern avifRGBImageAllocatePixels_pfn SK_avifRGBImageAllocatePixels;
extern avifRGBImageFreePixels_pfn     SK_avifRGBImageFreePixels;

// Structs

struct skiv_image_desktop_s {
  CComPtr <ID3D11ShaderResourceView> _srv              = nullptr;
  CComPtr <ID3D11Resource>           _res              = nullptr;
  bool                               _hdr_image        =   false;
  ImVec2                             _resolution       = ImVec2 (0.0f, 0.0f);
  ImVec2                             _desktop_pos      = ImVec2 (0.0f, 0.0f);
  DXGI_MODE_ROTATION                 _rotation         = DXGI_MODE_ROTATION_UNSPECIFIED;
  float                              _max_display_nits = 1000.0f;
  float                              _sdr_display_nits = 240.0f;

  bool process (void)
  {
    if (_srv != nullptr)
    {
      _srv->GetResource (&_res.p);

      if (_res != nullptr)
      {
        CComQIPtr <ID3D11Texture2D>
            pDesktopTex (_res);
        if (pDesktopTex != nullptr)
        {
          D3D11_TEXTURE2D_DESC   texDesc = { };
          pDesktopTex->GetDesc (&texDesc);

          _resolution.x = static_cast <float> (texDesc.Width);
          _resolution.y = static_cast <float> (texDesc.Height);

          // HDR formats indicates we are working with a HDR capture
          if (texDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM  ||
              texDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
              texDesc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT)
            _hdr_image = true;

#ifdef _DEBUG
          ImGui::InsertNotification ({
            ImGuiToastType::Info, 5000,
              "Screen Capture Data",
              "HDR: %i",
              _hdr_image
          });
#endif

          return true;
        }
      }
    }

    return false;
  }

  void clear (void)
  {
    _res              = nullptr;
    _srv              = nullptr;
    _hdr_image        =   false;
    _resolution       = ImVec2 (0.0f, 0.0f);
    _desktop_pos      = ImVec2 (0.0f, 0.0f);
    _max_display_nits = 1000.0f;
    _rotation         = DXGI_MODE_ROTATION_UNSPECIFIED;
  }
};