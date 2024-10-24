#pragma once

#include <SKIV.h>

// This file is included mostly everywhere else, so lets define using ImGui's math operators here.
#define IMGUI_DEFINE_MATH_OPERATORS

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <dxgitype.h>

enum SKIF_ImGuiAxis {
  SKIF_ImGuiAxis_None,
  SKIF_ImGuiAxis_X,
  SKIF_ImGuiAxis_Y,

  SKIF_ImGuiAxis_Both =
     SKIF_ImGuiAxis_X | SKIF_ImGuiAxis_Y
};


typedef unsigned int SelectionFlag;  // -> enum SelectionFlag_
enum SelectionFlag_
{
  SelectionFlag_None          = 0,
  SelectionFlag_SingleClick   = 1 << 0, // Select on a single click
  SelectionFlag_AllowInverted = 1 << 1, // Allow an inverted selection rectangle?
  SelectionFlag_Filled        = 1 << 2, // Fill out the selection rectangle

  // Multi-flags
  SelectionFlag_Default       =         // Default flags: SelectionFlag_Filled
    SelectionFlag_Filled
};

float    SKIF_ImGui_LinearTosRGB          (float col_lin);
ImVec4   SKIF_ImGui_LinearTosRGB          (ImVec4 col);
float    SKIF_ImGui_sRGBtoLinear          (float col_srgb);
ImVec4   SKIF_ImGui_sRGBtoLinear          (ImVec4 col);
void     SKIF_ImGui_StyleColorsDark       (ImGuiStyle* dst = nullptr);
void     SKIF_ImGui_StyleColorsLight      (ImGuiStyle* dst = nullptr);
void     SKIF_ImGui_AdjustAppModeSize     (HMONITOR monitor);
void     SKIF_ImGui_InfoMessage           (std::string szTitle, const std::string szLabel);
void     SKIF_ImGui_PopBackInfoPopup      (void);
void     SKIF_ImGui_CloseInfoPopup        (void);
bool     SKIF_ImGui_IsFocused             (void);
bool     SKIF_ImGui_IsMouseHovered        (void);
bool     SKIF_ImGui_IsAnyInputDown        (void);
bool     SKIF_ImGui_IsAnyPopupOpen        (void);
bool     SKIF_ImGui_SelectionRect         (ImRect* selection, ImRect allowed, ImGuiMouseButton mouse_button = ImGuiMouseButton_Left, SelectionFlag flags = SelectionFlag_Default);
void     SKIF_ImGui_SetMouseCursorHand    (void);
void     SKIF_ImGui_SetHoverTip           (const std::string_view& szText, bool ignoreDisabledTooltips = false);
void     SKIF_ImGui_SetHoverText          (const std::string_view& szText, bool overrideExistingText = false);
bool     SKIF_ImGui_BeginChildFrame       (ImGuiID id, const ImVec2& size, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags = ImGuiWindowFlags_None);
bool     SKIF_ImGui_BeginMainChildFrame   (ImGuiWindowFlags window_flags = ImGuiWindowFlags_None);
bool     SKIF_ImGui_BeginMenuEx2          (const char* label, const char* icon, const ImVec4& colIcon = ImGui::GetStyleColorVec4 (ImGuiCol_Text), bool enabled = true);
bool     SKIF_ImGui_MenuItemEx2           (const char* label, const char* icon, const ImVec4& colIcon = ImGui::GetStyleColorVec4 (ImGuiCol_Text), const char* shortcut = NULL, bool* p_selected = nullptr, bool enabled = true);
bool     SKIF_ImGui_IconButton            (ImGuiID id, std::string icon, std::string label, const ImVec4& colIcon = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption));
void     SKIF_ImGui_OptImage              (ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0), DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_IDENTITY);
void     SKIF_ImGui_Columns               (int columns_count, const char* id, bool border, bool resizeble = false);
void     SKIF_ImGui_Spacing               (float multiplier);
void     SKIF_ImGui_Spacing               (void);
bool     SKIF_ImGui_Selectable            (const char* label);
bool     SKIF_ImGui_SelectableVAligned    (const char* unique_id, const char* label, bool* p_selected, ImGuiSelectableFlags flags, const ImVec2& size_arg);
ImFont*  SKIF_ImGui_LoadFont              (const std::wstring& filename, float point_size, const ImWchar* glyph_range, ImFontConfig* cfg = nullptr);
void     SKIF_ImGui_InitFonts             (float fontSize, bool extendedCharsets = true);
void     SKIF_ImGui_SetStyle              (ImGuiStyle* dst = nullptr);
void     SKIF_ImGui_PushDisableState      (void);
void     SKIF_ImGui_PopDisableState       (void);
void     SKIF_ImGui_PushDisabledSpacing   (void);
void     SKIF_ImGui_PopDisabledSpacing    (void);
void     SKIF_ImGui_DisallowMouseDragMove (void); // Prevents SKIF from enabling drag move using the mouse
bool     SKIF_ImGui_CanMouseDragMove      (void); // True if drag move using the mouse is allowed, false if not
void     SKIF_ImGui_AutoScroll            (bool touch_only_on_void, SKIF_ImGuiAxis axis);
void     SKIF_ImGui_UpdateScrollbarState  (void); // Update the internal state tracking scrollbars
bool     SKIF_ImGui_IsScrollbarX          (void); // Helper function returning true if a horizontal scrollbar is visible
bool     SKIF_ImGui_IsScrollbarY          (void); // Helper function returning true if a vertical scrollbar is visible
bool     SKIF_ImGui_IsFullscreen          (HWND hWnd);
void     SKIF_ImGui_SetFullscreen         (HWND hWnd, bool fullscreen, HMONITOR monitor = NULL); // Set window to fullscreen on the current/specified monitor
void     SKIF_ImGui_InvalidateFonts       (void);
ImGuiKey SKIF_ImGui_CharToImGuiKey        (char c);

bool     SKIF_ImGui_IsViewportHDR         (HWND hWnd);
bool     SKIF_ImGui_IsViewportHDRCapable  (HWND hWnd);

// SKIF_ImGui_ImDerp, named as such as it is not a linear interpolation/lerp, is used
//   to among other things force 1.0f for the alpha color channel (w)
static ImVec4 SKIF_ImGui_ImDerp       (const ImVec4& a, const ImVec4& b, float t) { return ImVec4 (a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t), a.z + ((b.z - a.z) * t), 1.0f /*a.w + (b.w - a.w) * t */); }

// Message popups
extern PopupState  PopupMessageInfo;  // App Mode: show an informational message box with text set through SKIF_ImGui_InfoMessage

// Fonts
extern ImFont* fontConsolas;
