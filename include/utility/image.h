#pragma once

#include <DirectXTex.h>

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
  {  0.3592, -0.1922, 0.0070, 0.0 },
  {  0.6976,  1.1004, 0.0749, 0.0 },
  { -0.0358,  0.0755, 0.8434, 0.0 },
  {  0.0,     0.0,    0.0,    1.0 }
};

constexpr DirectX::XMMATRIX c_fromLMStoXYZ = // Transposed
{
  {  2.070180056695613509600,  0.364988250032657479740, -0.049595542238932107896, 0.0 },
  { -1.326456876103021025500,  0.680467362852235141020, -0.049421161186757487412, 0.0 },
  {  0.206616006847855170810, -0.045421753075853231409,  1.187995941732803439400, 0.0 },
  {  0.0,                      0.0,                      0.0,                     1.0 }
};

constexpr DirectX::XMMATRIX c_scRGBtoBt2100 = // Transposed
{
  { 2939026994.L /  585553224375.L,   76515593.L / 138420033750.L,    12225392.L /   93230009375.L, 0.0 },
  { 9255011753.L / 3513319346250.L, 6109575001.L / 830520202500.L,  1772384008.L / 2517210253125.L, 0.0 },
  {  173911579.L /  501902763750.L,   75493061.L / 830520202500.L, 18035212433.L / 2517210253125.L, 0.0 },
  {                            0.0,                           0.0,                             0.0, 1.0 }
};

constexpr DirectX::XMMATRIX c_Bt2100toscRGB = // Transposed
{
  {  348196442125.L / 1677558947.L, -579752563250.L / 37238079773.L,  -12183628000.L /  5369968309.L, 0.0f },
  { -123225331250.L / 1677558947.L, 5273377093000.L / 37238079773.L, -472592308000.L / 37589778163.L, 0.0f },
  {  -15276242500.L / 1677558947.L,  -38864558125.L / 37238079773.L, 5256599974375.L / 37589778163.L, 0.0f },
  {                           0.0f,                            0.0f,                            0.0f, 1.0f }
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

#pragma warning( pop ) 

// Declarations
DirectX::XMVECTOR SKIV_Image_PQToLinear    (DirectX::XMVECTOR N, DirectX::XMVECTOR maxPQValue = DirectX::g_XMOne);
DirectX::XMVECTOR SKIV_Image_LinearToPQ    (DirectX::XMVECTOR N, DirectX::XMVECTOR maxPQValue = DirectX::g_XMOne);
float             SKIV_Image_LinearToPQY   (float N);
DirectX::XMVECTOR SKIV_Image_Rec709toICtCp (DirectX::XMVECTOR N);
DirectX::XMVECTOR SKIV_Image_ICtCptoRec709 (DirectX::XMVECTOR N);

bool    SKIV_Image_CopyToClipboard (const DirectX::Image* pImage, bool snipped, bool isHDR, bool force_sRGB);
HRESULT SKIV_Image_SaveToDisk_HDR  (const DirectX::Image& image, const wchar_t* wszFileName);
HRESULT SKIV_Image_SaveToDisk_SDR  (const DirectX::Image& image, const wchar_t* wszFileName, bool force_sRGB);