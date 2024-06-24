#include <utility/skif_imgui.h>

#include <filesystem>
#include <fstream>
#include <unordered_set>

#include <utility/sk_utility.h>
#include <utility/utility.h>

#include <utility/fsutil.h>
#include <utility/registry.h>

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>
#include <fonts/fa_solid_900.ttf.h>
#include <fonts/fa_brands_400.ttf.h>
#include <imgui/imgui_impl_dx11.h>

bool                  bAutoScrollActive;
bool                  bScrollbarX;
bool                  bScrollbarY;
ImFont*               fontConsolas = nullptr;
std::vector <ImWchar> vFontChineseSimplified;
std::vector <ImWchar> vFontChineseAll;
std::vector <ImWchar> vFontCyrillic;
std::vector <ImWchar> vFontJapanese;
std::vector <ImWchar> vFontKorean;
std::vector <ImWchar> vFontThai;
std::vector <ImWchar> vFontVietnamese;
std::vector <ImWchar> vFontAwesome;
std::vector <ImWchar> vFontAwesomeBrands;

PopupState PopupMessageInfo = PopupState_Closed;
std::vector <std::string> vInfoMessage_Titles;
std::vector <std::string> vInfoMessage_Labels;

float
SKIF_ImGui_sRGBtoLinear (float col_srgb)
{
  return (col_srgb <= 0.04045f)
        ? col_srgb / 12.92f
        : pow ((col_srgb + 0.055f) / 1.055f, 2.4f);
}

ImVec4
SKIF_ImGui_sRGBtoLinear (ImVec4 col)
{
    col.x = SKIF_ImGui_sRGBtoLinear (col.x);
    col.y = SKIF_ImGui_sRGBtoLinear (col.y);
    col.z = SKIF_ImGui_sRGBtoLinear (col.z);
    col.w = SKIF_ImGui_sRGBtoLinear (col.w);

    return col;
}

float
SKIF_ImGui_LinearTosRGB (float col_lin)
{
  return (col_lin <= 0.0031308f)
        ? col_lin * 12.92f
        : 1.055f * pow (col_lin, 1.0f / 2.4f) - 0.055f;
}

ImVec4
SKIF_ImGui_LinearTosRGB (ImVec4 col)
{
    col.x = SKIF_ImGui_LinearTosRGB (col.x);
    col.y = SKIF_ImGui_LinearTosRGB (col.y);
    col.z = SKIF_ImGui_LinearTosRGB (col.z);
    col.w = SKIF_ImGui_LinearTosRGB (col.w);

    return col;
}


void
SKIF_ImGui_StyleColorsDark (ImGuiStyle* dst)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
  ImVec4* colors = style->Colors;

  // Text
  colors[ImGuiCol_Text]                   = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
  colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
  colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.30f); //ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

  // Window, Child, Popup
  colors[ImGuiCol_WindowBg]               = ImVec4(0.10f, 0.10f, 0.10f, 1.00f); // ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
  colors[ImGuiCol_ChildBg]                = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
  colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.08f, 1.00f); // ImVec4(0.08f, 0.08f, 0.08f, 0.90f);

  // Borders
  colors[ImGuiCol_Border]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
  colors[ImGuiCol_BorderShadow]           = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

  // Frame [Checkboxes, Radioboxes]
  colors[ImGuiCol_FrameBg]                = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
  colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
  colors[ImGuiCol_FrameBgActive]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

  // Title Background [Popups]
  colors[ImGuiCol_TitleBg]                = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
  colors[ImGuiCol_TitleBgActive]          = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.50f, 0.50f, 0.50f, 1.00f); // Unchanged

  // MenuBar
  colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);

  // CheckMark / Radio Button
  colors[ImGuiCol_CheckMark]              = ImVec4(0.45f, 0.45f, 0.45f, 1.00f); // ImVec4(0.26f, 0.59f, 0.98f, 1.00f)

  // Slider Grab (used for HDR Brightness slider)
  colors[ImGuiCol_SliderGrab]             = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
  colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

  // Buttons
  colors[ImGuiCol_Button]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
  colors[ImGuiCol_ButtonHovered]          = ImVec4(0.23f, 0.23f, 0.23f, 1.00f);
  colors[ImGuiCol_ButtonActive]           = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);

  // Headers [Selectables, CollapsibleHeaders]
  colors[ImGuiCol_Header]                 = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
  colors[ImGuiCol_HeaderHovered]          = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
  colors[ImGuiCol_HeaderActive]           = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

  // Scrollbar
  colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_ScrollbarGrab]          = colors[ImGuiCol_Header];
  colors[ImGuiCol_ScrollbarGrabHovered]   = colors[ImGuiCol_HeaderHovered];
  colors[ImGuiCol_ScrollbarGrabActive]    = colors[ImGuiCol_HeaderActive];

  // Separators
  colors[ImGuiCol_Separator]              = (_registry.bUIBorders) ? colors[ImGuiCol_Border]
                                                                   : ImVec4(0.15f, 0.15f, 0.15f, 1.00f); // colors[ImGuiCol_WindowBg];
  colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
  colors[ImGuiCol_SeparatorActive]        = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);

  // Resize Grip
  colors[ImGuiCol_ResizeGrip]             = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
  colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
  colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);

  // Tabs
  colors[ImGuiCol_Tab]                    = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);       //ImLerp(colors[ImGuiCol_Header],       colors[ImGuiCol_TitleBgActive], 0.80f);
  colors[ImGuiCol_TabHovered]             = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
  colors[ImGuiCol_TabActive]              = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);       //ImLerp(colors[ImGuiCol_HeaderActive], colors[ImGuiCol_TitleBgActive], 0.60f);
  colors[ImGuiCol_TabUnfocused]           = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
  colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);

  // Docking stuff
  colors[ImGuiCol_DockingPreview]         = colors[ImGuiCol_HeaderActive] * ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
  colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

  // Plot
  colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
  colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
  colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
  colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);

  // Tables
  colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
  colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);   // Prefer using Alpha=1.0 here
  colors[ImGuiCol_TableBorderLight]       = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);   // Prefer using Alpha=1.0 here
  colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);

  // Drag-n-drop
  colors[ImGuiCol_DragDropTarget]         = ImVec4(0.90f, 0.90f, 0.10f, 1.00f);

  // Nav/Modal
  // This is the white border that highlights selected items when using keyboard focus etc
  colors[ImGuiCol_NavHighlight]           = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
//colors[ImGuiCol_NavHighlight]           = ImVec4(0.48f, 0.48f, 0.48f, 1.00f);
//colors[ImGuiCol_NavHighlight]           = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
  colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.50f); // ImVec4(0.80f, 0.80f, 0.80f, 0.20f)
  colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.00f, 0.00f, 0.00f, 0.50f); // ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

  // Custom
  colors[ImGuiCol_SKIF_TextBase]          = ImVec4(0.68f, 0.68f, 0.68f, 1.00f);
  colors[ImGuiCol_SKIF_TextCaption]       = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
  colors[ImGuiCol_SKIF_TextGameTitle]     = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
  colors[ImGuiCol_SKIF_Success]           = ImColor(121, 214, 28);  // 42,  203, 2);  //53,  255, 3);  //ImColor(144, 238, 144);
  colors[ImGuiCol_SKIF_Warning]           = ImColor(255, 124, 3); // ImColor::HSV(0.11F, 1.F, 1.F)
  colors[ImGuiCol_SKIF_Failure]           = ImColor(186, 59, 61, 255);
  colors[ImGuiCol_SKIF_Info]              = ImVec4(0.26f, 0.59f, 0.98f, 1.00f); // colors[ImGuiCol_CheckMark];
  colors[ImGuiCol_SKIF_Yellow]            = ImColor::HSV(0.11F, 1.F, 1.F);
  colors[ImGuiCol_SKIF_Icon]              = colors[ImGuiCol_Text];
}

void
SKIF_ImGui_StyleColorsLight (ImGuiStyle* dst)
{
  ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
  ImVec4* colors = style->Colors;

  // Text
  colors[ImGuiCol_Text]                   = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
  colors[ImGuiCol_TextDisabled]           = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
  colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);

  // Window, Child, Popup
  colors[ImGuiCol_WindowBg]               = ImVec4(0.94f, 0.94f, 0.94f, 1.00f); // ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
  colors[ImGuiCol_ChildBg]                = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
  colors[ImGuiCol_PopupBg]                = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);

  // Borders
  colors[ImGuiCol_Border]                 = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
  colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

  // Frame [Checkboxes, Radioboxes]
  colors[ImGuiCol_FrameBg]                = ImVec4(0.85f, 0.85f, 0.85f, 1.00f); // ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.78f, 0.78f, 0.78f, 1.00f); // ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
  colors[ImGuiCol_FrameBgActive]          = ImVec4(0.90f, 0.90f, 0.90f, 1.00f); // ImVec4(0.26f, 0.59f, 0.98f, 0.67f);

  // Title Background [Popups]
  colors[ImGuiCol_TitleBg]                = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
  colors[ImGuiCol_TitleBgActive]          = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(1.00f, 1.00f, 1.00f, 0.51f);

  // MenuBar
  colors[ImGuiCol_MenuBarBg]              = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);

  // CheckMark / Radio Button
  colors[ImGuiCol_CheckMark]              = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

  // Slider Grab (used for HDR Brightness slider)
  colors[ImGuiCol_SliderGrab]             = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
  colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.46f, 0.54f, 0.80f, 0.60f);

  // Buttons
  colors[ImGuiCol_Button]                 = ImVec4(0.85f, 0.85f, 0.85f, 1.00f); // ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
  colors[ImGuiCol_ButtonHovered]          = ImVec4(0.90f, 0.90f, 0.90f, 1.00f); // ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
  colors[ImGuiCol_ButtonActive]           = ImVec4(0.95f, 0.95f, 0.95f, 1.00f); // ImVec4(0.06f, 0.53f, 0.98f, 1.00f);

  // Headers [Selectables, CollapsibleHeaders]
  colors[ImGuiCol_Header]                 = ImVec4(1.00f, 1.00f, 1.00f, 1.00f); // ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
  colors[ImGuiCol_HeaderHovered]          = ImVec4(0.88f, 0.88f, 0.88f, 1.00f); // ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
  colors[ImGuiCol_HeaderActive]           = ImVec4(0.80f, 0.80f, 0.80f, 1.00f); // ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

  // Scrollbar
  colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.98f, 0.98f, 0.98f, 0.00f);
  colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.69f, 0.69f, 0.69f, 0.80f);
  colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.49f, 0.49f, 0.49f, 0.80f);
  colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.49f, 0.49f, 0.49f, 1.00f);

  // Separators
  colors[ImGuiCol_Separator]              = ImVec4(0.39f, 0.39f, 0.39f, 0.62f);
  colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.14f, 0.44f, 0.80f, 0.78f);
  colors[ImGuiCol_SeparatorActive]        = ImVec4(0.14f, 0.44f, 0.80f, 1.00f);

  // Resize Grip
  colors[ImGuiCol_ResizeGrip]             = ImVec4(0.80f, 0.80f, 0.80f, 0.56f);
  colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
  colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);

  // Tabs
  colors[ImGuiCol_Tab]                    = ImVec4(0.85f, 0.85f, 0.85f, 1.00f); // ImLerp(colors[ImGuiCol_Header],       colors[ImGuiCol_TitleBgActive], 0.90f);
  colors[ImGuiCol_TabHovered]             = ImVec4(0.90f, 0.90f, 0.90f, 1.00f); // colors[ImGuiCol_HeaderHovered];
  colors[ImGuiCol_TabActive]              = ImVec4(0.80f, 0.80f, 0.80f, 1.00f); // ImLerp(colors[ImGuiCol_HeaderActive], colors[ImGuiCol_TitleBgActive], 0.60f);
  colors[ImGuiCol_TabUnfocused]           = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
  colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);

  // Docking stuff
  colors[ImGuiCol_DockingPreview]         = colors[ImGuiCol_Header] * ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
  colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

  // Plot
  colors[ImGuiCol_PlotLines]              = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
  colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
  colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
  colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.45f, 0.00f, 1.00f);

  // Tables
  colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.78f, 0.87f, 0.98f, 1.00f);
  colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.57f, 0.57f, 0.64f, 1.00f);   // Prefer using Alpha=1.0 here
  colors[ImGuiCol_TableBorderLight]       = ImVec4(0.68f, 0.68f, 0.74f, 1.00f);   // Prefer using Alpha=1.0 here
  colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.30f, 0.30f, 0.30f, 0.09f);

  // Drag-n-drop
  colors[ImGuiCol_DragDropTarget]         = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);

  // Nav/Modal
  colors[ImGuiCol_NavHighlight]           = colors[ImGuiCol_HeaderHovered];
  colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(0.70f, 0.70f, 0.70f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.20f, 0.20f, 0.20f, 0.20f);
  colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);

  // Custom
  colors[ImGuiCol_SKIF_TextBase]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
  colors[ImGuiCol_SKIF_TextCaption]       = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
  colors[ImGuiCol_SKIF_TextGameTitle]     = colors[ImGuiCol_Text];
  colors[ImGuiCol_SKIF_Success]           = ImColor(86, 168, 64); //144, 238, 144) * ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
  colors[ImGuiCol_SKIF_Warning]           = ImColor(240, 139, 24); // ImColor::HSV(0.11F, 1.F, 1.F)
  colors[ImGuiCol_SKIF_Failure]           = ImColor(186, 59, 61, 255);
  colors[ImGuiCol_SKIF_Info]              = colors[ImGuiCol_CheckMark];
  colors[ImGuiCol_SKIF_Yellow]            = ImColor::HSV(0.11F, 1.F, 1.F);
  colors[ImGuiCol_SKIF_Icon]              = colors[ImGuiCol_Text];
}

void
SKIF_ImGui_AdjustAppModeSize (HMONITOR monitor)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  extern ImVec2 SKIF_vecRegularModeDefault;
  extern ImVec2 SKIF_vecRegularModeAdjusted;
  extern ImVec2 SKIF_vecAlteredSize;
  extern float  SKIF_fStatusBarHeight;
  extern float  SKIF_fStatusBarDisabled;
  extern float  SKIF_fStatusBarHeightTips;

  // Reset reduced height
  SKIF_vecAlteredSize.y = 0.0f;

  // Adjust the large mode size
  SKIF_vecRegularModeAdjusted = SKIF_vecRegularModeDefault;

  // Add the status bar if it is not disabled
  if (_registry.bUIStatusBar)
  {
    SKIF_vecRegularModeAdjusted.y += SKIF_fStatusBarHeight;

    if (! _registry.bUITooltips)
    {
      SKIF_vecRegularModeAdjusted.y += SKIF_fStatusBarHeightTips;
    }
  }

  else
  {
    SKIF_vecRegularModeAdjusted.y += SKIF_fStatusBarDisabled;
  }

  // Take the current display into account
  if (monitor == NULL && SKIF_ImGui_hWnd != NULL)
    monitor = ::MonitorFromWindow (SKIF_ImGui_hWnd, MONITOR_DEFAULTTONEAREST);

  MONITORINFO
    info        = {                  };
    info.cbSize = sizeof (MONITORINFO);

  if (monitor != NULL && ::GetMonitorInfo (monitor, &info))
  {
    ImVec2 WorkSize =
      ImVec2 ( (float)( info.rcWork.right  - info.rcWork.left ),
                (float)( info.rcWork.bottom - info.rcWork.top  ) );

    ImVec2 tmpCurrentSize  = SKIF_vecRegularModeAdjusted ;

    if (tmpCurrentSize.y * SKIF_ImGui_GlobalDPIScale > (WorkSize.y))
      SKIF_vecAlteredSize.y = (tmpCurrentSize.y * SKIF_ImGui_GlobalDPIScale - (WorkSize.y));
  }
}

void
SKIF_ImGui_InfoMessage (std::string szTitle, const std::string szLabel)
{
  szTitle = "(" + std::to_string (vInfoMessage_Titles.size() + 1) + ") " + szTitle;
  vInfoMessage_Titles.push_back (szTitle);
  vInfoMessage_Labels.push_back (szLabel);
}

void
SKIF_ImGui_PopBackInfoPopup (void)
{
  if (! vInfoMessage_Labels.empty())
  {
    vInfoMessage_Titles.pop_back();
    vInfoMessage_Labels.pop_back();
  }

  if (vInfoMessage_Labels.empty())
    PopupMessageInfo = PopupState_Closed;
}

// Internal; processes any existing info messages
void
SKIF_ImGui_InfoMessage_Process (void)
{
  if (! vInfoMessage_Labels.empty())
  {
    static float fPopupWidth;
    fPopupWidth = ImGui::CalcTextSize (vInfoMessage_Labels.back().c_str()).x + ImGui::GetStyle().IndentSpacing * 3.0f; // 60.0f * SKIF_ImGui_GlobalDPIScale
    ImGui::OpenPopup ("###PopupMessageInfo");
    ImGui::SetNextWindowSize (ImVec2 (fPopupWidth, 0.0f));

    if (PopupMessageInfo == PopupState_Closed)
      PopupMessageInfo = PopupState_Open;

    ImGui::SetNextWindowPos    (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));
    if (ImGui::BeginPopupModal ((vInfoMessage_Titles.back() + "###PopupMessageInfo").c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
    {
      if (PopupMessageInfo == PopupState_Open)
      {
        // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
        ImGuiWindow* window = ImGui::FindWindowByName ("###PopupMessageInfo");
        if (window != nullptr && ! window->Appearing)
          PopupMessageInfo = PopupState_Opened;
      }

      ImGui::TreePush    ("PopupMessageInfoTreePush");

      SKIF_ImGui_Spacing ( );

      ImGui::Text        (vInfoMessage_Labels.back().c_str());

      SKIF_ImGui_Spacing ( );
      SKIF_ImGui_Spacing ( );

      ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

      ImGui::SetCursorPosX (fPopupWidth / 2 - vButtonSize.x / 2);

      if (ImGui::Button  ("OK", vButtonSize))
      {
        vInfoMessage_Titles.pop_back();
        vInfoMessage_Labels.pop_back();

        if (vInfoMessage_Labels.empty())
          PopupMessageInfo = PopupState_Closed;

        ImGui::CloseCurrentPopup ( );
      }

      SKIF_ImGui_DisallowMouseDragMove ( );

      SKIF_ImGui_Spacing ( );

      ImGui::TreePop     ( );

      ImGui::EndPopup ( );
    }
  }
}

void
SKIF_ImGui_CloseInfoPopup (void)
{
  if (PopupMessageInfo != PopupState_Closed)
  {
    vInfoMessage_Labels.clear();
    vInfoMessage_Titles.clear();
    PopupMessageInfo = PopupState_Closed;

    ImGui::ClosePopupsOverWindow (ImGui::GetCurrentWindowRead(), true);
  }
}

bool
SKIF_ImGui_IsFocused (void)
{
  extern bool SKIF_ImGui_ImplWin32_IsFocused (void);
  return SKIF_ImGui_ImplWin32_IsFocused ( );
}

bool
SKIF_ImGui_IsMouseHovered (void)
{
  POINT                 mouse_screen_pos = { };
  if (!::GetCursorPos (&mouse_screen_pos))
    return false;

  // See if we are currently hovering over one of our viewports
  if (HWND hovered_hwnd = ::WindowFromPoint (mouse_screen_pos))
    if (NULL != ImGui::FindViewportByPlatformHandle ((void *)hovered_hwnd))
      return true; // We are in fact hovering over something

  return false;
}

bool
SKIF_ImGui_IsAnyInputDown (void)
{
  for (ImGuiKey key = ImGuiKey_KeysData_OFFSET; key < ImGuiKey_COUNT; key = (ImGuiKey)(key + 1))
    if (ImGui::IsKeyDown(key))
      return true;

  if (bAutoScrollActive)
    return true;

  return false;
}

bool
SKIF_ImGui_IsAnyPopupOpen (void)
{
  return ImGui::IsPopupOpen ("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

bool
SKIF_ImGui_SelectionRect (ImRect*          selection,
                          ImRect           allowed,
                          ImGuiMouseButton mouse_button,
                          SelectionFlag    flags)
{
  const bool single_click =
    (flags & SelectionFlag_SingleClick);

  const bool min_is_zero =
    (selection->Min.x == selection->Min.y && selection->Min.y == 0.0f);

  // Update start position on click
  if ((ImGui::IsMouseClicked (mouse_button) && (! single_click)) ||
                                  (min_is_zero && single_click))
    selection->Min = ImGui::GetMousePos ();

  // Update end position while being held down
  if (ImGui::IsMouseDown (mouse_button) || single_click)
    selection->Max = ImGui::GetMousePos ();

  // Keep the selection within the allowed rectangle
  selection->ClipWithFull (allowed);

  static const auto
    inset = ImVec2 (0.0f, 0.0f);

  // Draw the selection rectangle
  if (ImGui::IsMouseDown (mouse_button) || single_click)
  {
    ImDrawList* draw_list =
      ImGui::GetForegroundDrawList ();

    draw_list->AddRect       (selection->Min-inset, selection->Max+inset, ImGui::GetColorU32 (IM_COL32(0,130,216,255)), 0.0f, 0, 5.0f); // Border

    if (flags & SelectionFlag_Filled)
      draw_list->AddRectFilled (selection->Min,       selection->Max,       ImGui::GetColorU32 (IM_COL32(0,130,216,50)));  // Background
  }

  const bool complete =
    ( single_click && GetAsyncKeyState (VK_LBUTTON)) ||
    (!single_click && ImGui::IsMouseReleased (mouse_button));

  const bool allow_inversion =
    (flags & SelectionFlag_AllowInverted);

  if (complete)
  {
    if (! allow_inversion)
    {
      if (selection->Min.x > selection->Max.x) std::swap (selection->Min.x, selection->Max.x);
      if (selection->Min.y > selection->Max.y) std::swap (selection->Min.y, selection->Max.y);
    }
  }

  return
    complete;
}

void
SKIF_ImGui_SetMouseCursorHand (void)
{
  // Only change the cursor if the current item is actually being hovered **and** the cursor is the one hovering it.
  // IsItemHovered() fixes cursor changing for overlapping items, and IsMouseHoveringRect() fixes cursor changing due to keyboard/gamepad selections
  if (ImGui::IsItemHovered ( ) && ImGui::IsMouseHoveringRect (ImGui::GetItemRectMin( ), ImGui::GetItemRectMax( )))
  {
    extern bool SKIF_MouseDragMoveAllowed;
    SKIF_MouseDragMoveAllowed = false;
    ImGui::SetMouseCursor (
      ImGuiMouseCursor_Hand
    );
  }
}

void
SKIF_ImGui_SetHoverTip (const std::string_view& szText, bool ignoreDisabledTooltips)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  extern bool        HoverTipActive;        // Used to track if an item is being hovered
  extern DWORD       HoverTipDuration;      // Used to track how long the item has been hovered (to delay showing tooltips)
  extern std::string SKIF_StatusBarText;

  if (ImGui::IsItemHovered () && ! szText.empty())
  {
    if (_registry.bUITooltips || ignoreDisabledTooltips)
    {
      ImGui::PushStyleColor (ImGuiCol_PopupBg,  ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));
      ImGui::PushStyleColor (ImGuiCol_Text,     ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg));
      HoverTipActive = true;

      if ( HoverTipDuration == 0)
      {
        HoverTipDuration = SKIF_Util_timeGetTime ( );

        // Use a timer to force SKIF to refresh once the duration has passed
        SetTimer (SKIF_Notify_hWnd,
            IDT_REFRESH_TOOLTIP,
            550,
            (TIMERPROC) NULL
        );
      }

      else if ( HoverTipDuration + 500 < SKIF_Util_timeGetTime() )
      {
        ImGui::SetTooltip (
          "%hs", szText.data ()
        );
      }

      ImGui::PopStyleColor  ( );
      ImGui::PopStyleColor  ( );
    }

    else
    {
      SKIF_StatusBarText =
        "Info: ";

      SKIF_ImGui_SetHoverText (
        szText.data (), true
      );
    }
  }
}

void
SKIF_ImGui_SetHoverText ( const std::string_view& szText,
                              bool  overrideExistingText )
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  extern std::string SKIF_StatusBarHelp;

  if ( ImGui::IsItemHovered ()                                  &&
        ( overrideExistingText || SKIF_StatusBarHelp.empty () )
     )
  {
    extern ImVec2 SKIF_vecCurrentMode;

    // If the text is wider than the app window is, use a hover tooltip if possible
    if (_registry.bUITooltips && ImGui::CalcTextSize (szText.data()).x > (SKIF_vecCurrentMode.x - 100.0f * SKIF_ImGui_GlobalDPIScale)) // -100px due to the Add Game option
      SKIF_ImGui_SetHoverTip (szText);
    else
      SKIF_StatusBarHelp.assign (szText);
  }
}

void
SKIF_ImGui_Spacing (float multiplier)
{
  ImGui::ItemSize (
    ImVec2 ( ImGui::GetTextLineHeightWithSpacing () * multiplier,
             ImGui::GetTextLineHeightWithSpacing () * multiplier )
  );
}

void
SKIF_ImGui_Spacing (void)
{
  SKIF_ImGui_Spacing (0.25f);
}

// Difference to regular Selectable? Doesn't span further than the width of the label!
bool
SKIF_ImGui_Selectable (const char* label)
{
  return ImGui::Selectable  (label, false, ImGuiSelectableFlags_None, ImGui::CalcTextSize (label, NULL, true));
}

// Vertical aligned label
// Must be surrounded by ImGui::PushID/PopID
bool
SKIF_ImGui_SelectableVAligned (const char* unique_id, const char* label, bool* p_selected, ImGuiSelectableFlags flags, const ImVec2& size_arg)
{
  ImVec2 cursor_pre    =  ImGui::GetCursorPos ( );
  ImVec2 label_size    =  ImGui::CalcTextSize (label);
  float  label_offset  = (size_arg.y - label_size.y) / 2.0f;
  ImVec2 label_pos     =  ImVec2 (cursor_pre.x + ImGui::GetStyle().ItemSpacing.x, cursor_pre.y + label_offset);

  ImGui::BeginGroup      ( );
  //ImGui::PushID          (unique_id);
  ImGui::PushStyleVar    (ImGuiStyleVar_FrameBorderSize, 0.0f);
  // We use ImGuiSelectableFlags_NoPadWithHalfSpacing to prevent the bounding box from having 0.5 * ItemSpacing padded on top and bottom
  bool   ret           =  ImGui::Selectable (unique_id, p_selected, flags | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_NoPadWithHalfSpacing, size_arg);
  ImVec2 cursor_post   =  ImGui::GetCursorPos ( );
  ImGui::SetCursorPos    (label_pos);
  ImGui::TextUnformatted (label);
  ImGui::PopStyleVar     ( );
  //ImGui::PopID           ( );
  ImGui::EndGroup        ( );

  ImGui::SetCursorPos    (cursor_post);
  return ret;
}

// Difference to regular BeginChildFrame? No ImGuiWindowFlags_NoMove!
bool
SKIF_ImGui_BeginChildFrame (ImGuiID id, const ImVec2& size, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags)
{
  const ImGuiStyle& style =
    ImGui::GetStyle ();

  //ImGui::PushStyleColor (ImGuiCol_ChildBg,              style.Colors [ImGuiCol_FrameBg]);
  ImGui::PushStyleVar   (ImGuiStyleVar_ChildRounding,   style.FrameRounding);
  ImGui::PushStyleVar   (ImGuiStyleVar_ChildBorderSize, style.FrameBorderSize);
  ImGui::PushStyleVar   (ImGuiStyleVar_WindowPadding,   style.FramePadding);

  bool ret =
    ImGui::BeginChild (id, size, ImGuiChildFlags_AlwaysUseWindowPadding | child_flags, window_flags);

  ImGui::PopStyleVar   (3);
  //ImGui::PopStyleColor ( );

  return ret;
}

// Basically like ImGui::Image but, you know, doesn't actually draw the images
void SKIF_ImGui_OptImage (ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
{
  // If not a nullptr, run original code
  if (user_texture_id != nullptr)
  {
    ImGui::Image (user_texture_id, size, uv0, uv1, tint_col, border_col);
  }
  
  // If a nullptr, run slightly tweaked code that omitts the image rendering
  else {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);
    if (border_col.w > 0.0f)
        bb.Max += ImVec2(2, 2);
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, 0))
        return;

    if (border_col.w > 0.0f)
    {
        window->DrawList->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(border_col), 0.0f);
        //window->DrawList->AddImage(user_texture_id, bb.Min + ImVec2(1, 1), bb.Max - ImVec2(1, 1), uv0, uv1, ImGui::GetColorU32(tint_col));
    }
    else
    {
        //window->DrawList->AddImage(user_texture_id, bb.Min, bb.Max, uv0, uv1, ImGui::GetColorU32(tint_col));
    }
  }
}

// Difference from regular? Who knows
void
SKIF_ImGui_Columns (int columns_count, const char* id, bool border, bool resizeble)
{
  ImGuiWindow* window = ImGui::GetCurrentWindowRead();
  IM_ASSERT(columns_count >= 1);

  ImGuiOldColumnFlags flags = (border ? 0 : ImGuiOldColumnFlags_NoBorder);
  if (! resizeble)
    flags |= ImGuiOldColumnFlags_NoResize;
  //flags |= ImGuiOldColumnFlags_NoPreserveWidths; // NB: Legacy behavior
  ImGuiOldColumns* columns = window->DC.CurrentColumns;
  if (columns != NULL && columns->Count == columns_count && columns->Flags == flags)
    return;

  if (columns != NULL)
    ImGui::EndColumns();

  if (columns_count != 1)
    ImGui::BeginColumns(id, columns_count, flags);
}

// This is used to set up the main content area for all tabs
bool SKIF_ImGui_BeginMainChildFrame (ImGuiWindowFlags window_flags)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  extern ImVec2 SKIF_vecAlteredSize;
  extern float  SKIF_fStatusBarDisabled;  // Status bar disabled

  static ImGuiID frame_content_area_id =
    ImGui::GetID ("###SKIF_CONTENT_AREA");

  return SKIF_ImGui_BeginChildFrame (
    frame_content_area_id,
    ImVec2 (0.0f, 0.0f),
    ImGuiChildFlags_None, // ImGuiChildFlags_AlwaysUseWindowPadding, // ImGuiChildFlags_FrameStyle
    ImGuiWindowFlags_NavFlattened | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_HorizontalScrollbar | window_flags
  );
}

bool
SKIF_ImGui_BeginMenuEx2 (const char* label, const char* icon, const ImVec4& colIcon, bool enabled)
{
  ImGui::PushStyleColor         (ImGuiCol_SKIF_Icon, colIcon);
  bool ret = ImGui::BeginMenuEx (label, icon, enabled);
  ImGui::PopStyleColor          ( );

  return ret;
}

bool
SKIF_ImGui_MenuItemEx2 (const char* label, const char* icon, const ImVec4& colIcon, const char* shortcut, bool* p_selected, bool enabled)
{
  ImGui::PushStyleColor        (ImGuiCol_SKIF_Icon, colIcon);
  bool ret = ImGui::MenuItemEx (label, icon, shortcut, p_selected ? *p_selected : false, enabled);
  if (ret && p_selected)
    *p_selected = !*p_selected;
  ImGui::PopStyleColor         ( );

  return ret;
}

bool SKIF_ImGui_IconButton (ImGuiID id, std::string icon, std::string label, const ImVec4& colIcon)
{
  bool ret   = false;
  icon       = " " + icon;
  label      = label + " ";

  ImGui::BeginChild (id, ImVec2 (ImGui::CalcTextSize(icon.c_str())  .x +
                                 ImGui::CalcTextSize(label.c_str()) .x +
                                 ImGui::CalcTextSize("    ").x,
                                 ImGui::GetTextLineHeightWithSpacing() + 2.0f * SKIF_ImGui_GlobalDPIScale),
    ImGuiChildFlags_FrameStyle,
    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NavFlattened
  );

  ImVec2 iconPos = ImGui::GetCursorPos ( );
  ImGui::ItemSize      (ImVec2 (ImGui::CalcTextSize (icon.c_str()) .x, ImGui::GetTextLineHeightWithSpacing()));
  ImGui::SameLine      ( );
  ImGui::Selectable    (label.c_str(), &ret,  ImGuiSelectableFlags_SpanAllColumns | static_cast<int>(ImGuiSelectableFlags_SpanAvailWidth));
  ImGui::SetCursorPos  (iconPos);
  ImGui::TextColored   (colIcon, icon.c_str());

  ImGui::EndChild ( );

  return ret;
}


// Fonts


const ImWchar*
SK_ImGui_GetGlyphRangesDefaultEx (void)
{
  static const ImWchar ranges [] =
  {
    0x0020,  0x00FF, // Basic Latin + Latin Supplement
    0x0100,  0x03FF, // Latin, IPA, Greek
    0x2000,  0x206F, // General Punctuation
    0x207f,  0x2090, // N/A (literally, the symbols for N/A :P)
    0x2100,  0x21FF, // Letterlike Symbols
    0x2500,  0x257F, // Box Drawing (needed for U+2514)
    0x2600,  0x26FF, // Misc. Characters
    0x2700,  0x27BF, // Dingbats
    0xc2b1,  0xc2b3, // ²
    0
  };
  return &ranges [0];
}

const ImWchar*
SK_ImGui_GetGlyphRangesKorean (void)
{
  static const ImWchar ranges[] =
  {
      0x0020, 0x00FF, // Basic Latin + Latin Supplement
      0x3131, 0x3163, // Korean alphabets
//#ifdef _WIN64
      0xAC00, 0xD7A3, // Korean characters (Hangul syllables) -- should not be included on 32-bit OSes due to system limitations
//#endif
      0,
  };
  return &ranges[0];
}

const ImWchar*
SK_ImGui_GetGlyphRangesFontAwesome (void)
{
  static const ImWchar ranges [] =
  {
    0x0020, 0x00FF,             // Basic Latin + Latin Supplement  (this is needed to get the missing glyph fallback to work)
    ICON_MIN_FA,  ICON_MAX_FA,  // Font Awesome (Solid / Regular)
    0
  };
  return &ranges [0];
}

const ImWchar*
SK_ImGui_GetGlyphRangesFontAwesomeBrands (void)
{
  static const ImWchar ranges [] =
  {
    0x0020, 0x00FF,             // Basic Latin + Latin Supplement  (this is needed to get the missing glyph fallback to work)
    ICON_MIN_FAB, ICON_MAX_FAB, // Font Awesome (Brands)
    0
  };
  return &ranges [0];
}

void
SKIF_ImGui_MissingGlyphCallback (wchar_t c)
{
  static UINT acp = GetACP();

  static std::unordered_set <wchar_t>
      unprintable_chars;

  if (unprintable_chars.emplace (c).second)
  {
    using range_def_s =
      std::pair <const ImWchar*, std::vector<ImWchar> * >; // bool *, 

    static       auto pFonts = ImGui::GetIO ().Fonts;

    // Sorted from least number of unique characters to the most
    static const auto ranges =
      {
      // Font Awesome doesn't work as intended as regular and brand has overlapping ranges...
        range_def_s { SK_ImGui_GetGlyphRangesFontAwesome            (), &vFontAwesome           },
      //range_def_s { SK_ImGui_GetGlyphRangesFontAwesomeBrands      (), &vFontAwesomeBrands     },
        range_def_s { pFonts->GetGlyphRangesVietnamese              (), &vFontVietnamese        },
        range_def_s { pFonts->GetGlyphRangesCyrillic                (), &vFontCyrillic          },
        range_def_s { pFonts->GetGlyphRangesThai                    (), &vFontThai              },
      ((acp == 932) // Prioritize Japanese for ACP 932
      ? range_def_s { pFonts->GetGlyphRangesJapanese                (), &vFontJapanese          }
      : range_def_s { pFonts->GetGlyphRangesChineseSimplifiedCommon (), &vFontChineseSimplified }),
      ((acp == 932)
      ? range_def_s { pFonts->GetGlyphRangesChineseSimplifiedCommon (), &vFontChineseSimplified }
      : range_def_s { pFonts->GetGlyphRangesJapanese                (), &vFontJapanese          }),
        range_def_s { pFonts->GetGlyphRangesKorean                  (), &vFontKorean            }
#ifdef _WIN64
      // 32-bit SKIF breaks if too many character sets are
      //   loaded so omit Chinese Full on those versions.
      , range_def_s { pFonts->GetGlyphRangesChineseFull             (), &vFontChineseAll        }
#endif
      };

    for ( const auto &[span, vector] : ranges)
    {
      ImWchar const *sp =
        &span [2]; // This here ends up excluding fonts that only define a single range, as [2] == 0 in those cases

      while (*sp != 0x0)
      {
        if ( c <= (wchar_t)(*sp++) &&
             c >= (wchar_t)(*sp++) )
        {
          sp             = nullptr;
          //*enable         = true;

          if (vector->empty())
            vector->push_back(c);
          else
            vector->back() = c;

          vector->push_back(c);

          // List is null terminated
          vector->push_back(0);

          extern bool invalidateFonts;
          invalidateFonts = true;

          break;
        }
      }

      if (sp == nullptr)
        break;
    }
  }
}

ImFont*
SKIF_ImGui_LoadFont ( const std::wstring& filename, float point_size, const ImWchar* glyph_range, ImFontConfig* cfg )
{
  auto& io =
    ImGui::GetIO ();

  wchar_t wszFullPath [MAX_PATH + 2] = { };

  if (GetFileAttributesW (              filename.c_str ()) != INVALID_FILE_ATTRIBUTES)
     wcsncpy_s ( wszFullPath, MAX_PATH, filename.c_str (),
                             _TRUNCATE );

  else
  {
    wchar_t     wszFontsDir [MAX_PATH] = { };
    wcsncpy_s ( wszFontsDir, MAX_PATH,
             SK_GetFontsDir ().c_str (), _TRUNCATE );

    PathCombineW ( wszFullPath,
                   wszFontsDir, filename.c_str () );

    if (GetFileAttributesW (wszFullPath) == INVALID_FILE_ATTRIBUTES)
      *wszFullPath = L'\0';
  }

  if (*wszFullPath != L'\0')
  {
    return
      io.Fonts->AddFontFromFileTTF ( SK_WideCharToUTF8 (wszFullPath).c_str (),
                                       point_size,
                                         cfg,
                                           glyph_range );
  }

  return (ImFont *)nullptr;
}

void
SKIF_ImGui_InitFonts (float fontSize, bool extendedCharsets)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  // Font size should always be rounded down to the nearest integer
  fontSize = floor(fontSize);

  static UINT acp = GetACP();

  auto& io =
    ImGui::GetIO ();

  extern ImGuiContext *GImGui;

  // Reset any existing fonts
  if (io.Fonts != nullptr)
  {
    if (GImGui->FontAtlasOwnedByContext)
    {
      if (GImGui->Font != nullptr)
      {
        GImGui->Font->ClearOutputData ();

        if (GImGui->Font->ContainerAtlas != nullptr)
            GImGui->Font->ContainerAtlas->Clear ();
      }

      io.FontDefault = nullptr;

      IM_DELETE (io.Fonts);
                 io.Fonts = IM_NEW (ImFontAtlas)();
    }
  }

  ImFontConfig
  font_cfg           = {  };
  
  std::filesystem::path fontDir
          (_path_cache.skiv_userdata);

  fontDir /= L"Fonts";

  bool useDefaultFont = false;
  if  (useDefaultFont)
  {
    font_cfg.SizePixels = 13.0f; // Size of the default font (default: 13.0f)
    io.Fonts->AddFontDefault (&font_cfg); // ProggyClean.ttf
    font_cfg.MergeMode = true;
  }

  std::wstring standardFont = (fontSize >= 18.0F) ? L"Tahoma.ttf" : L"Verdana.ttf"; // L"Tahoma.ttf" : L"Verdana.ttf";

  std::error_code ec;
  // Create any missing directories
  if (! std::filesystem::exists (            fontDir, ec))
        std::filesystem::create_directories (fontDir, ec);

  // Core character set
  SKIF_ImGui_LoadFont     (standardFont, fontSize, SK_ImGui_GetGlyphRangesDefaultEx(), &font_cfg);
  //SKIF_ImGui_LoadFont     ((fontDir / L"NotoSans-Regular.ttf"), fontSize, SK_ImGui_GetGlyphRangesDefaultEx());

  if (! useDefaultFont)
  {
    font_cfg.MergeMode = true;
  }

  // Load extended character sets when SKIF is not used as a launcher
  if (extendedCharsets)
  {
    // Cyrillic character set
    if (! vFontCyrillic.empty())
      SKIF_ImGui_LoadFont   (standardFont,   fontSize, vFontCyrillic.data(), &font_cfg);
      //SKIF_ImGui_LoadFont   ((fontDir / L"NotoSans-Regular.ttf"), fontSize, io.Fonts->GetGlyphRangesCyrillic        (), &font_cfg);
  
    // Japanese character set
    // Load before Chinese for ACP 932 so that the Japanese font is not overwritten
    if (! vFontJapanese.empty() && acp == 932)
    {
      //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansJP-Regular.ttf"), fontSize, io.Fonts->GetGlyphRangesJapanese        (), &font_cfg);
      ///*
      if (SKIF_Util_IsWindows10OrGreater ( ))
        SKIF_ImGui_LoadFont (L"YuGothR.ttc",  fontSize, vFontJapanese.data(), &font_cfg);
      else
        SKIF_ImGui_LoadFont (L"yugothic.ttf", fontSize, vFontJapanese.data(), &font_cfg);
      //*/
    }

    // Simplified Chinese character set
    // Also includes almost all of the Japanese characters except for some Kanjis
    if (! vFontChineseSimplified.empty())
      SKIF_ImGui_LoadFont   (L"msyh.ttc",     fontSize, vFontChineseSimplified.data(), &font_cfg);
      //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansSC-Regular.ttf"), fontSize, io.Fonts->GetGlyphRangesChineseSimplifiedCommon        (), &font_cfg);

    // Japanese character set
    // Load after Chinese for the rest of ACP's so that the Chinese font is not overwritten
    if (! vFontJapanese.empty() && acp != 932)
    {
      //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansJP-Regular.ttf"), fontSize, io.Fonts->GetGlyphRangesJapanese        (), &font_cfg);
      ///*
      if (SKIF_Util_IsWindows10OrGreater ( ))
        SKIF_ImGui_LoadFont (L"YuGothR.ttc",  fontSize, vFontJapanese.data(), &font_cfg);
      else
        SKIF_ImGui_LoadFont (L"yugothic.ttf", fontSize, vFontJapanese.data(), &font_cfg);
      //*/
    }
    
    // All Chinese character sets
    if (! vFontChineseAll.empty())
      SKIF_ImGui_LoadFont   (L"msjh.ttc",     fontSize, vFontChineseAll.data(), &font_cfg);
      //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansTC-Regular.ttf"), fontSize, io.Fonts->GetGlyphRangesChineseFull        (), &font_cfg);

    // Korean character set
    // On 32-bit builds this does not include Hangul syllables due to system limitaitons
    if (! vFontKorean.empty())
      SKIF_ImGui_LoadFont   (L"malgun.ttf",   fontSize, vFontKorean.data(), &font_cfg);
      //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansKR-Regular.ttf"), fontSize, io.Fonts->SK_ImGui_GetGlyphRangesKorean        (), &font_cfg);

    // Thai character set
    if (! vFontThai.empty())
      SKIF_ImGui_LoadFont   (standardFont,   fontSize, vFontThai.data(), &font_cfg);
      //SKIF_ImGui_LoadFont   ((fontDir / L"NotoSansThai-Regular.ttf"),   fontSize, io.Fonts->GetGlyphRangesThai      (), &font_cfg);

    // Vietnamese character set
    if (! vFontVietnamese.empty())
      SKIF_ImGui_LoadFont   (standardFont,   fontSize, vFontVietnamese.data(), &font_cfg);
      //SKIF_ImGui_LoadFont   ((fontDir / L"NotoSans-Regular.ttf"),   fontSize, io.Fonts->GetGlyphRangesVietnamese    (), &font_cfg);
  }

    static auto
      skif_fs_wb = ( std::ios_base::binary
                    | std::ios_base::out  );

    auto _UnpackFontIfNeeded =
    [&]( const char*   szFont,
          const uint8_t akData [],
          const size_t  cbSize )
    {
      if (! std::filesystem::is_regular_file ( fontDir / szFont, ec)        )
                        std::ofstream ( fontDir / szFont, skif_fs_wb ).
        write ( reinterpret_cast <const char *> (akData),
                                                  cbSize);
    };

    auto      awesome_fonts = {
      std::make_tuple (
        FONT_ICON_FILE_NAME_FAS, fa_solid_900_ttf,
                      _ARRAYSIZE (fa_solid_900_ttf) ),
      std::make_tuple (
        FONT_ICON_FILE_NAME_FAB, fa_brands_400_ttf,
                      _ARRAYSIZE (fa_brands_400_ttf) )
                              };

    float fontSizeFA       = fontSize - 2.0f;
    float fontSizeConsolas = fontSize - 4.0f;

    for (auto& font : awesome_fonts)
      _UnpackFontIfNeeded ( std::get <0> (font), std::get <1> (font), std::get <2> (font) );

    // In January, 2024 various optimization attempts were made to on-demand load Font Awesome characters
    //   as they appeared, but while this shaved down a bit on the initial launch time, it also resulted
    //     in unnecessary ~20ms delays every time a new Font Awesome character appeared on-screen...
    //        ... Meanwhile it only shaved of like 10-15ms on the launch time, lol ! // Aemony

    // FA Regular is basically useless as it only has 163 icons, so we don't bother using it
    // FA Solid has 1390 icons in comparison
        SKIF_ImGui_LoadFont (fontDir/FONT_ICON_FILE_NAME_FAS, fontSizeFA, SK_ImGui_GetGlyphRangesFontAwesome(), &font_cfg);
    // FA Brands
    SKIF_ImGui_LoadFont (fontDir/FONT_ICON_FILE_NAME_FAB, fontSizeFA, SK_ImGui_GetGlyphRangesFontAwesomeBrands(), &font_cfg);

    //io.Fonts->AddFontDefault ();

    fontConsolas = SKIF_ImGui_LoadFont (L"Consola.ttf", fontSizeConsolas, SK_ImGui_GetGlyphRangesDefaultEx ( ));
    //fontConsolas = SKIF_ImGui_LoadFont ((fontDir / L"NotoSansMono-Regular.ttf"), fontSize/* - 4.0f*/, SK_ImGui_GetGlyphRangesDefaultEx());
}

void
SKIF_ImGui_SetStyle (ImGuiStyle* dst)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );
  
  if (dst == nullptr)
    dst = &ImGui::GetStyle ( );

  _registry._StyleLightMode = false;

  // Setup Dear ImGui style
  switch (_registry.iStyle)
  {
  case UIStyle_ImGui_Dark:
    ImGui::StyleColorsDark      (dst);
    break;
  case UIStyle_ImGui_Classic:
    ImGui::StyleColorsClassic   (dst);
    break;
  case UIStyle_SKIF_Light:
    SKIF_ImGui_StyleColorsLight (dst);
    _registry._StyleLightMode = true;
    break;
  case UIStyle_SKIF_Dark:
    SKIF_ImGui_StyleColorsDark  (dst);
    break;
  case UIStyle_Dynamic:
  default:
    _registry.iStyle          = 0;
    _registry._StyleLightMode = static_cast<bool> (_registry.regKVWindowUseLightTheme.getData ( ));

    if (_registry._StyleLightMode)
      SKIF_ImGui_StyleColorsLight (dst);
    else
      SKIF_ImGui_StyleColorsDark  (dst);
    break;
  }

  // Override the style with a few tweaks of our own
  dst->DisabledAlpha   = 1.0f; // Disable the default 60% alpha transparency for disabled items
//dst->WindowPadding   = { };
//dst->FramePadding    = { };
  dst->WindowRounding  = 4.0F; // 4.0F; // style.ScrollbarRounding;
  dst->ChildRounding   = dst->WindowRounding;
  dst->TabRounding     = dst->WindowRounding;
  dst->FrameRounding   = dst->WindowRounding;

  // Touch input adjustment
  if (SKIF_Util_IsTouchCapable ( ))
  {
    _registry._TouchDevice = true;
    dst->ScrollbarSize = 50.0f;
  }

  else
    _registry._TouchDevice = false;
  
  if (! _registry.bUIBorders)
  {
    dst->TabBorderSize   = 0.0F;
    dst->FrameBorderSize = 0.0F;

    // Necessary to hide the 1 px separator shown at the bottom of the tabs row
    dst->Colors[ImGuiCol_TabActive] = dst->Colors[ImGuiCol_WindowBg];
  }

  else {
    // Is not scaled by ScaleAllSizes() so we have to do it here
    dst->TabBorderSize   = 1.0F * SKIF_ImGui_GlobalDPIScale;
    dst->FrameBorderSize = 1.0F * SKIF_ImGui_GlobalDPIScale;
  }

  // Scale the style based on the current DPI factor
  dst->ScaleAllSizes (SKIF_ImGui_GlobalDPIScale);
  
  if (_registry._sRGBColors)
    for (int i=0; i < ImGuiCol_COUNT; i++)
        dst->Colors[i] = SKIF_ImGui_sRGBtoLinear (dst->Colors[i]);

  // Hide the resize grip for all styles
  dst->Colors[ImGuiCol_ResizeGrip]        = ImVec4(0, 0, 0, 0);
  dst->Colors[ImGuiCol_ResizeGripActive]  = ImVec4(0, 0, 0, 0);
  dst->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0, 0, 0, 0);

  ImGui::GetStyle ( ) = *dst;
}

void
SKIF_ImGui_PushDisableState (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  // Push the states in a specific order
  ImGui::PushItemFlag   (ImGuiItemFlags_Disabled, true);
  //ImGui::PushStyleVar (ImGuiStyleVar_Alpha,     ImGui::GetStyle ().Alpha * 0.5f); // [UNUSED]
  ImGui::PushStyleColor (ImGuiCol_Text,           ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
  ImGui::PushStyleColor (ImGuiCol_SliderGrab,     SKIF_ImGui_ImDerp (ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg), ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), (_registry._StyleLightMode) ? 0.75f : 0.25f));
  ImGui::PushStyleColor (ImGuiCol_CheckMark,      SKIF_ImGui_ImDerp (ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg), ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), (_registry._StyleLightMode) ? 0.75f : 0.25f));
  ImGui::PushStyleColor (ImGuiCol_FrameBg,        SKIF_ImGui_ImDerp (ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg), ImGui::GetStyleColorVec4 (ImGuiCol_FrameBg),      (_registry._StyleLightMode) ? 0.75f : 0.15f));
}

void
SKIF_ImGui_PopDisableState (void)
{
  // Pop the states in the reverse order that we pushed them in
  ImGui::PopStyleColor (4); // ImGuiCol_FrameBg, ImGuiCol_CheckMark, ImGuiCol_SliderGrab, ImGuiCol_Text
  //ImGui::PopStyleVar ( ); // ImGuiStyleVar_Alpha [UNUSED]
  ImGui::PopItemFlag   ( ); // ImGuiItemFlags_Disabled
}

void
SKIF_ImGui_PushDisabledSpacing (void)
{
  // Remove borders, paddings, and spacing
  ImGui::PushStyleVar (ImGuiStyleVar_FrameBorderSize,  0.0f);
  ImGui::PushStyleVar (ImGuiStyleVar_ChildBorderSize,  0.0f);
  ImGui::PushStyleVar (ImGuiStyleVar_FrameRounding,    0.0f);
  ImGui::PushStyleVar (ImGuiStyleVar_ChildRounding,    0.0f);
  ImGui::PushStyleVar (ImGuiStyleVar_FramePadding,     ImVec2 (0, 0));
  ImGui::PushStyleVar (ImGuiStyleVar_IndentSpacing,    0.0f);
  ImGui::PushStyleVar (ImGuiStyleVar_ItemSpacing,      ImVec2 (0, 0));
  ImGui::PushStyleVar (ImGuiStyleVar_ItemInnerSpacing, ImVec2 (0, 0));
}

void
SKIF_ImGui_PopDisabledSpacing (void)
{
  ImGui::PopStyleVar  (8);
}

void
SKIF_ImGui_DisallowMouseDragMove (void)
{
  extern bool SKIF_MouseDragMoveAllowed;

  if (ImGui::IsItemActive ( ))
    SKIF_MouseDragMoveAllowed = false;
}

// Allows moving the window but only in certain circumstances
bool
SKIF_ImGui_CanMouseDragMove (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  extern bool SKIF_MouseDragMoveAllowed;
  return    ! _registry._TouchDevice      &&        // Only if we are not on a touch input device
              SKIF_MouseDragMoveAllowed   &&        // Manually disabled by a few UI elements
            ! ImGui::IsAnyItemHovered ( ) &&        // Disabled if any item is hovered
          ( ! SKIF_ImGui_IsAnyPopupOpen ( )      || // Disabled if any popup is opened..
          (PopupMessageInfo == PopupState_Opened || //   which are actually aligned to
         UpdatePromptPopup  == PopupState_Opened ||
              HistoryPopup  == PopupState_Opened ||
           AutoUpdatePopup  == PopupState_Opened ));
}

// Based on https://github.com/ocornut/imgui/issues/3379#issuecomment-1678718752
void
SKIF_ImGui_TouchScrollWhenDragging (bool only_on_void, SKIF_ImGuiAxis axis)
{
  ImGuiContext& g = *ImGui::GetCurrentContext();
  ImGuiWindow* window = g.CurrentWindow;
  bool hovered = false;
  ImGuiID id = window->GetID("##scrolldraggingoverlay");
  ImGui::KeepAliveID(id);
  if (! only_on_void || g.HoveredId == 0) // If nothing hovered so far in the frame (not same as IsAnyItemHovered()!)
    ImGui::ButtonBehavior(window->Rect(), id, &hovered, &bAutoScrollActive, ImGuiMouseButton_Left);

  if (bAutoScrollActive)
  {
    ImVec2 delta = ImGui::GetIO().MouseDelta;

    if ((axis & SKIF_ImGuiAxis_X) && delta.x != 0.0f) // Horizontal
      ImGui::SetScrollX (window, window->Scroll.x - delta.x);

    if ((axis & SKIF_ImGuiAxis_Y) && delta.y != 0.0f) // Vertical
      ImGui::SetScrollY (window, window->Scroll.y - delta.y);
  }
}

void
SKIF_ImGui_MouseWheelScroll (SKIF_ImGuiAxis axis)
{
  ImGuiContext& g = *ImGui::GetCurrentContext();
  ImGuiWindow* window = g.CurrentWindow;
  ImGuiID id = window->GetID("##scrollwheeloverlay");
  ImGui::KeepAliveID(id);

  static ImVec2 position;
  static bool held = false;

  if (bAutoScrollActive && ! SKIF_ImGui_IsFocused ( ))
    bAutoScrollActive = held = false;

  else if (bAutoScrollActive &&
     (ImGui::GetKeyData (ImGuiKey_MouseLeft  )->DownDuration == 0.0f ||
      ImGui::GetKeyData (ImGuiKey_MouseMiddle)->DownDuration == 0.0f ||
      ImGui::GetKeyData (ImGuiKey_MouseRight )->DownDuration == 0.0f))
    bAutoScrollActive = held = false;

  else if (bAutoScrollActive && held &&
           ImGui::GetKeyData (ImGuiKey_MouseMiddle)->DownDuration < 0.0f)
    bAutoScrollActive = held = false;

  else if (bAutoScrollActive &&
           ImGui::GetKeyData (ImGuiKey_MouseMiddle)->DownDuration > 0.15f) // Special handling if mouse wheel is held down for longer than 150 ms
    held = true;

//else if (! onVoid || g.HoveredId == 0) // If nothing hovered so far in the frame (not same as IsAnyItemHovered()!)
//else if (ImGui::IsMousePosValid ( ) && ImGui::ButtonBehavior  (window->Rect(), id, &hovered, &held, ImGuiButtonFlags_AllowOverlap | ImGuiButtonFlags_MouseButtonMiddle | static_cast<ImGuiButtonFlags_> (ImGuiButtonFlags_PressedOnClick)))
  else if (ImGui::IsMousePosValid ( ) && ImGui::GetKeyData (ImGuiKey_MouseMiddle)->DownDuration == 0.0f && ImGui::IsMouseHoveringRect (window->Rect().Min, window->Rect().Max) )
  {
    bAutoScrollActive = true;
    position = ImGui::GetMousePos ( );
  }

  if (bAutoScrollActive && ImGui::IsMousePosValid ( ))
  {
    ImVec2 delta = position - ImGui::GetMousePos ( );

    switch (axis)
    {
    case SKIF_ImGuiAxis_None:
      break;
    case SKIF_ImGuiAxis_X:
      ImGui::SetMouseCursor (ImGuiMouseCursor_ResizeEW);
      break;
    case SKIF_ImGuiAxis_Y:
      ImGui::SetMouseCursor (ImGuiMouseCursor_ResizeNS);
      break;
    case SKIF_ImGuiAxis_Both:
      ImGui::SetMouseCursor (ImGuiMouseCursor_ResizeAll);
      break;
    default:
      break;
    }

    if ((axis & SKIF_ImGuiAxis_X) && delta.x != 0.0f) // Horizontal
      ImGui::SetScrollX (window, window->Scroll.x - delta.x * 0.1f);

    if ((axis & SKIF_ImGuiAxis_Y) && delta.y != 0.0f) // Vertical
      ImGui::SetScrollY (window, window->Scroll.y - delta.y * 0.1f);

    extern bool SKIF_MouseDragMoveAllowed;
    SKIF_MouseDragMoveAllowed = false;
  }
}

void
SKIF_ImGui_AutoScroll (bool touch_only_on_void, SKIF_ImGuiAxis axis)
{
  if (axis == SKIF_ImGuiAxis_None)
    return;

  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  // This disables drag-move on the list of games and should only be enabled on touch devices
  // Allows drag-scrolling using the left mouse button
  if (_registry._TouchDevice)
    SKIF_ImGui_TouchScrollWhenDragging (touch_only_on_void, axis);

  // This allows mouse wheel scroll (aka autoscroll) in non-touch mode
  else
    SKIF_ImGui_MouseWheelScroll (axis);
}

void
SKIF_ImGui_UpdateScrollbarState (void)
{
  if (ImGui::GetCurrentWindowRead() != NULL)
  {
    bScrollbarX = ImGui::GetCurrentWindowRead()->ScrollbarX;
    bScrollbarY = ImGui::GetCurrentWindowRead()->ScrollbarY;
  }

  else {
    bScrollbarX = false;
    bScrollbarY = false;
  }
}

bool
SKIF_ImGui_IsScrollbarX (void)
{
  return bScrollbarX;
}

bool
SKIF_ImGui_IsScrollbarY (void)
{
  return bScrollbarY;
}

bool
SKIF_ImGui_IsFullscreen (HWND hWnd)
{
  extern bool SKIF_ImGui_ImplWin32_SetFullscreen (HWND, int, HMONITOR);
  return SKIF_ImGui_ImplWin32_SetFullscreen      (hWnd, -1, NULL);
}

void
SKIF_ImGui_SetFullscreen (HWND hWnd, bool fullscreen, HMONITOR monitor)
{
  extern bool SKIF_ImGui_ImplWin32_SetFullscreen (HWND, int, HMONITOR);
  SKIF_ImGui_ImplWin32_SetFullscreen             (hWnd, static_cast<int> (fullscreen), monitor);
}

void
SKIF_ImGui_InvalidateFonts (void)
{
  extern float SKIF_ImGui_FontSizeDefault;

  float fontScale = SKIF_ImGui_FontSizeDefault * SKIF_ImGui_GlobalDPIScale;

  SKIF_ImGui_InitFonts (fontScale); // SKIF_FONTSIZE_DEFAULT);

  if (ImGui::GetIO().Fonts->TexPixelsAlpha8 == NULL)
  {
    DWORD temp_time = SKIF_Util_timeGetTime1();
    ImGui::GetIO ().Fonts->Build ( );
    PLOG_DEBUG << "Operation [Fonts->Build] took " << (SKIF_Util_timeGetTime1() - temp_time) << " ms.";
  }

  ImGui_ImplDX11_InvalidateDeviceObjects ( );
}

// This helper function maps char to ImGuiKey_xxx
// For use with e.g. ImGui::GetKeyData ( )
ImGuiKey
SKIF_ImGui_CharToImGuiKey (char c)
{
  switch (c)
  {
    case ' ':  return ImGuiKey_Space;
    case '\'': return ImGuiKey_Apostrophe;
    case ',':  return ImGuiKey_Comma;
    case '-':  return ImGuiKey_Minus;
    case '.':  return ImGuiKey_Period;
    case '/':  return ImGuiKey_Slash;
  //case ':':  return ImGuiKey_Colon; // ???
    case ';':  return ImGuiKey_Semicolon;
    case '0':  return ImGuiKey_0;
    case '1':  return ImGuiKey_1;
    case '2':  return ImGuiKey_2;
    case '3':  return ImGuiKey_3;
    case '4':  return ImGuiKey_4;
    case '5':  return ImGuiKey_5;
    case '6':  return ImGuiKey_6;
    case '7':  return ImGuiKey_7;
    case '8':  return ImGuiKey_8;
    case '9':  return ImGuiKey_9;
    case 'A':  return ImGuiKey_A;
    case 'B':  return ImGuiKey_B;
    case 'C':  return ImGuiKey_C;
    case 'D':  return ImGuiKey_D;
    case 'E':  return ImGuiKey_E;
    case 'F':  return ImGuiKey_F;
    case 'G':  return ImGuiKey_G;
    case 'H':  return ImGuiKey_H;
    case 'I':  return ImGuiKey_I;
    case 'J':  return ImGuiKey_J;
    case 'K':  return ImGuiKey_K;
    case 'L':  return ImGuiKey_L;
    case 'M':  return ImGuiKey_M;
    case 'N':  return ImGuiKey_N;
    case 'O':  return ImGuiKey_O;
    case 'P':  return ImGuiKey_P;
    case 'Q':  return ImGuiKey_Q;
    case 'R':  return ImGuiKey_R;
    case 'S':  return ImGuiKey_S;
    case 'T':  return ImGuiKey_T;
    case 'U':  return ImGuiKey_U;
    case 'V':  return ImGuiKey_V;
    case 'W':  return ImGuiKey_W;
    case 'X':  return ImGuiKey_X;
    case 'Y':  return ImGuiKey_Y;
    case 'Z':  return ImGuiKey_Z;
    default:   return ImGuiKey_None;
  }
}
