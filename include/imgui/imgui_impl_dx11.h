// dear imgui: Renderer Backend for DirectX11
// This needs to be used along with a Platform Backend (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'ID3D11ShaderResourceView*' as ImTextureID. Read the FAQ about ImTextureID!
//  [X] Renderer: Large meshes support (64k+ vertices) with 16-bit indices.
//  [X] Renderer: Multi-viewport support (multiple windows). Enable with 'io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable'.

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#pragma once
#include "imgui/imgui.h"      // IMGUI_IMPL_API
#ifndef IMGUI_DISABLE

struct ID3D11Device;
struct ID3D11DeviceContext;

IMGUI_IMPL_API bool     ImGui_ImplDX11_Init(ID3D11Device* device, ID3D11DeviceContext* device_context);
IMGUI_IMPL_API void     ImGui_ImplDX11_Shutdown();
IMGUI_IMPL_API void     ImGui_ImplDX11_NewFrame();
IMGUI_IMPL_API void     ImGui_ImplDX11_RenderDrawData(ImDrawData* draw_data);

// Use if you want to reset your rendering device without losing Dear ImGui state.
IMGUI_IMPL_API void     ImGui_ImplDX11_InvalidateDeviceObjects();
IMGUI_IMPL_API bool     ImGui_ImplDX11_CreateDeviceObjects();


#ifndef SKIF_CUSTOM_IMGUI_DX11_VIEWPORT_STRUCT
#define SKIF_CUSTOM_IMGUI_DX11_VIEWPORT_STRUCT
#include <dxgi1_2.h>
#include <dxgi1_6.h>
#include <d3d11.h>

struct ImGui_ImplDX11_ViewportData
{
    IDXGISwapChain1*        SwapChain;
    ID3D11RenderTargetView* RTView;
    UINT                    PresentCount;
    HANDLE                  WaitHandle;
    int                     SDRMode;       // 0 = 8 bpc,   1 = 10 bpc,      2 = 16 bpc scRGB
    FLOAT                   SDRWhiteLevel; // SDR white level in nits for the display
    bool                    HDR;
    bool                    HDRCapable;
    int                     HDRMode;       // 0 = No HDR,  1 = 10 bpc HDR,  2 = 16 bpc scRGB HDR
    FLOAT                   HDRLuma;
    FLOAT                   HDRMinLuma;
    DXGI_OUTPUT_DESC1       DXGIDesc;
    DXGI_FORMAT             DXGIFormat;

     ImGui_ImplDX11_ViewportData (void) {            SwapChain  = nullptr;   RTView  = nullptr;   WaitHandle  = 0;  PresentCount = 0; SDRMode = 0; SDRWhiteLevel = 80.0f; HDRMode = 0; HDR = false; HDRCapable = false; HDRLuma = 0.0f; HDRMinLuma = 0.0f; DXGIDesc = {   }; DXGIFormat = DXGI_FORMAT_UNKNOWN; }
    ~ImGui_ImplDX11_ViewportData (void) { IM_ASSERT (SwapChain == nullptr && RTView == nullptr && WaitHandle == 0); }
};
#endif


#endif // #ifndef IMGUI_DISABLE
