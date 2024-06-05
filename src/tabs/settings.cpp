
#include <utility/skif_imgui.h>
#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>
#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <filesystem>

#include <dxgi.h>
#include <d3d11.h>
#include <d3dkmthk.h>

#include <utility/fsutil.h>
#include <utility/registry.h>
#include <utility/updater.h>
#include <utility/gamepad.h>
#include "../../version.h"
#include <tabs/common_ui.h>

extern bool allowShortcutCtrlA;

void
SKIF_UI_Tab_DrawSettings (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  bool goBack = false;
  
  if (ImGui::Button (ICON_FA_LEFT_LONG " Go back###GoBackBtn1", ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale, 30.0f * SKIF_ImGui_GlobalDPIScale)))
    SKIF_Tab_ChangeTo = UITab_Viewer;

  SKIF_ImGui_Spacing ( );
  SKIF_ImGui_Spacing ( );
  
  ImGui::PushStyleColor   (
    ImGuiCol_SKIF_TextCaption, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption) * ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
                            );
    
  ImGui::PushStyleColor   (
    ImGuiCol_CheckMark, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption)
                            );

  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Components:"
  );
    
  ImGui::PushStyleColor   (
    ImGuiCol_SKIF_TextBase, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                            );
    
  ImGui::PushStyleColor   (
    ImGuiCol_TextDisabled, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) * ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
                            );

  SKIF_ImGui_Spacing      ( );
  
  SKIF_UI_DrawComponentVersion ( );

  ImGui::PopStyleColor    (4);

  ImGui::Spacing ();
  ImGui::Spacing ();

#pragma region Section: Image

  if (ImGui::CollapsingHeader ("Images###SKIF_SettingsHeader-1", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::PushStyleColor   (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                              );

    SKIF_ImGui_Spacing      ( );

    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Scaling method:"
    );
    ImGui::TreePush        ("ImageScaling");

    //if (ImGui::RadioButton ("Never",           &_registry.iAutoStopBehavior, 0))
    //  regKVAutoStopBehavior.putData (           _registry.iAutoStopBehavior);
    // 
    //ImGui::SameLine        ( );

    if (ImGui::RadioButton ("None",       &_registry.iImageScaling, 0))
      _registry.regKVImageScaling.putData (_registry.iImageScaling);

    ImGui::SameLine        ( );

    if (ImGui::RadioButton ("Fill",       &_registry.iImageScaling, 1))
      _registry.regKVImageScaling.putData (_registry.iImageScaling);

    ImGui::SameLine        ( );

    if (ImGui::RadioButton ("Fit",        &_registry.iImageScaling, 2))
      _registry.regKVImageScaling.putData (_registry.iImageScaling);

    ImGui::SameLine        ( );

    if (ImGui::RadioButton ("Stretch",    &_registry.iImageScaling, 3))
      _registry.regKVImageScaling.putData (_registry.iImageScaling);

    ImGui::TreePop         ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Useful if you find bright images an annoyance.");
    ImGui::SameLine        ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Darken images by 25%%:"
    );
    ImGui::TreePush        ("DarkenImages");
    if (ImGui::RadioButton ("Never",                 &_registry.iDarkenImages, 0))
      _registry.regKVDarkenImages.putData (                        _registry.iDarkenImages);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Always",                &_registry.iDarkenImages, 1))
      _registry.regKVDarkenImages.putData (                        _registry.iDarkenImages);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Based on mouse cursor", &_registry.iDarkenImages, 2))
      _registry.regKVDarkenImages.putData (                        _registry.iDarkenImages);
    ImGui::TreePop         ( );

    ImGui::PopStyleColor();
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion


#pragma region Section: Appearances
  if (ImGui::CollapsingHeader ("Appearance###SKIF_SettingsHeader-2", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::PushStyleColor   (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                              );

    extern bool RecreateSwapChains;
    extern bool RecreateWin32Windows;

    SKIF_ImGui_Spacing      ( );

    constexpr char* StyleItems[UIStyle_COUNT] =
    { "Dynamic",
      "SKIF Dark",
      "SKIF Light",
      "ImGui Classic",
      "ImGui Dark"
    };
    static const char*
      StyleItemsCurrent;
      StyleItemsCurrent = StyleItems[_registry.iStyle]; // Re-apply the value on every frame as it may have changed
          
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Color theme:"
    );
    ImGui::TreePush      ("ColorThemes");

    if (ImGui::GetContentRegionAvail().x > 725.0f)
      ImGui::SetNextItemWidth (500.0f);

    if (ImGui::BeginCombo ("###_registry.iStyleCombo", StyleItemsCurrent)) // The second parameter is the label previewed before opening the combo.
    {
      for (int n = 0; n < UIStyle_COUNT; n++)
      {
        bool is_selected = (StyleItemsCurrent == StyleItems[n]); // You can store your selection however you want, outside or inside your objects
        if (ImGui::Selectable (StyleItems[n], is_selected))
          _registry.iStyleTemp = n;         // We apply the new style at the beginning of the next frame to prevent any PushStyleColor/Var from causing issues
        if (is_selected)
          ImGui::SetItemDefaultFocus ( );   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
      }
      ImGui::EndCombo  ( );
    }

    ImGui::TreePop       ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Move the mouse over each option to get more information.");
    ImGui::SameLine        ( );
    ImGui::TextColored     (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "UI elements:"
    );
    ImGui::TreePush        ("UIElements");

    ImGui::BeginGroup ( );

    if (ImGui::Checkbox ("Borders",    &_registry.bUIBorders))
    {
      _registry.regKVUIBorders.putData (_registry.bUIBorders);

      ImGuiStyle            newStyle;
      SKIF_ImGui_SetStyle (&newStyle);
    }

    SKIF_ImGui_SetHoverTip ("Use borders around UI elements.");

    if (ImGui::Checkbox ("Tooltips",    &_registry.bUITooltips))
    {
      _registry.regKVUITooltips.putData (_registry.bUITooltips);

      // Adjust the app mode size
      SKIF_ImGui_AdjustAppModeSize (NULL);
    }

    if (ImGui::IsItemHovered ())
      SKIF_StatusBarText = "Info: ";

    SKIF_ImGui_SetHoverText ("This is instead where additional information will be displayed.");
    SKIF_ImGui_SetHoverTip  ("If tooltips are disabled the status bar will be used for additional information.\n"
                             "Note that some links cannot be previewed as a result.");

    if (ImGui::Checkbox ("Status bar",   &_registry.bUIStatusBar))
    {
      _registry.regKVUIStatusBar.putData (_registry.bUIStatusBar);

      // Adjust the app mode size
      SKIF_ImGui_AdjustAppModeSize (NULL);
    }

    SKIF_ImGui_SetHoverTip ("Disabling the status bar as well as tooltips will hide all additional information or tips.");

    ImGui::EndGroup ( );

    ImGui::SameLine ( );
    ImGui::Spacing  ( ); // New column
    ImGui::SameLine ( );

    ImGui::BeginGroup ( );

    if (ImGui::Checkbox ("Caption buttons", &_registry.bUICaptionButtons))
      _registry.regKVUICaptionButtons.putData (_registry.bUICaptionButtons);

    SKIF_ImGui_SetHoverTip ("Show the caption buttons of the window.");

    if (ImGui::Checkbox ("Fade covers", &_registry.bFadeCovers))
    {
      _registry.regKVFadeCovers.putData (_registry.bFadeCovers);

      extern float fAlpha;
      fAlpha = (_registry.bFadeCovers) ?   0.0f   : 1.0f;
    }

    SKIF_ImGui_SetHoverTip ("Fade between game covers when switching games.");

    if (SKIF_Util_IsWindows11orGreater ( ))
    {
      if ( ImGui::Checkbox ( "Win11 corners", &_registry.bWin11Corners) )
      {
        _registry.regKVWin11Corners.putData (  _registry.bWin11Corners);
        
        // Force recreating the window on changes
        RecreateWin32Windows = true;
      }

      SKIF_ImGui_SetHoverTip ("Use rounded window corners.");
    }

    ImGui::EndGroup ( );

    ImGui::SameLine ( );
    ImGui::Spacing  ( ); // New column
    ImGui::SameLine ( );

    ImGui::BeginGroup ( );

    if ( ImGui::Checkbox ( "Touch input", &_registry.bTouchInput) )
    {
      _registry.regKVTouchInput.putData (  _registry.bTouchInput);

      ImGuiStyle            newStyle;
      SKIF_ImGui_SetStyle (&newStyle);
    }

    SKIF_ImGui_SetHoverTip ("Make the UI easier to use on touch input capable devices automatically.");

    if (ImGui::Checkbox ("HiDPI scaling", &_registry.bDPIScaling))
    {
      extern bool
        changedHiDPIScaling;
        changedHiDPIScaling = true;
    }

    SKIF_ImGui_SetHoverTip ("Disabling HiDPI scaling will make the application appear smaller on HiDPI displays.");

    if (ImGui::Checkbox ("Shelly the Ghost", &_registry.bGhost))
      _registry.regKVGhost.putData (  _registry.bGhost);

    SKIF_ImGui_SetHoverTip ("Every time the UI renders a frame, Shelly the Ghost moves a little bit.");

    ImGui::EndGroup ( );

    if (! _registry.bUITooltips &&
        ! _registry.bUIStatusBar)
    {
      ImGui::BeginGroup  ( );
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      ImGui::SameLine    ( );
      ImGui::TextColored (ImColor(0.68F, 0.68F, 0.68F, 1.0f), "Context based information and tips will not appear!");
      ImGui::EndGroup    ( );

      SKIF_ImGui_SetHoverTip ("Restore context based information and tips by enabling tooltips or the status bar.", true);
    }

    ImGui::TreePop       ( );

    ImGui::Spacing         ( );

#pragma region Appearance::Renderer

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Move the mouse over each option to get more information");
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                            "UI mode:"
    );

    ImGui::TreePush        ("SKIF_iUIMode");
    // Flip VRR Compatibility Mode (only relevant on Windows 10+)
    if (SKIF_Util_IsWindows10OrGreater ( ))
    {
      if (ImGui::RadioButton ("VRR Compatibility", &_registry.iUIMode, 2))
      {
        _registry.regKVUIMode.putData (             _registry.iUIMode);
        RecreateSwapChains = true;
      }
      SKIF_ImGui_SetHoverTip ("Avoids signal loss and flickering on VRR displays.");
      ImGui::SameLine        ( );
    }
    if (ImGui::RadioButton ("Normal",              &_registry.iUIMode, 1))
    {
      _registry.regKVUIMode.putData (               _registry.iUIMode);
      RecreateSwapChains = true;
    }
    SKIF_ImGui_SetHoverTip ("Improves UI response on low fixed-refresh rate displays.");
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Safe Mode",           &_registry.iUIMode, 0))
    {
      _registry.regKVUIMode.putData (               _registry.iUIMode);
      RecreateSwapChains = true;
    }
    SKIF_ImGui_SetHoverTip ("Compatibility mode for users experiencing issues with the other two modes.");
    ImGui::TreePop         ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Increases the color depth of the app.");
    ImGui::SameLine        ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Color depth:"
    );
    
    static int placeholder = 0;
    static int* ptrSDR = nullptr;

    if ((_registry.iHDRMode > 0 && SKIF_Util_IsHDRActive ( )))
    {
      SKIF_ImGui_PushDisableState ( );

      ptrSDR = &_registry.iHDRMode;
    }
    else
      ptrSDR = &_registry.iSDRMode;
    
    ImGui::TreePush        ("iSDRMode");
    if (ImGui::RadioButton   ("8 bpc",        ptrSDR, 0))
    {
      _registry.regKVSDRMode.putData (_registry.iSDRMode);
      RecreateSwapChains = true;
    }
    // It seems that Windows 10 1709+ (Build 16299) is required to
    // support 10 bpc (DXGI_FORMAT_R10G10B10A2_UNORM) for flip model
    if (SKIF_Util_IsWindows10v1709OrGreater ( ))
    {
      ImGui::SameLine        ( );
      if (ImGui::RadioButton ("10 bpc",       ptrSDR, 1))
      {
        _registry.regKVSDRMode.putData (_registry.iSDRMode);
        RecreateSwapChains = true;
      }
    }
    ImGui::SameLine        ( );
    if (ImGui::RadioButton   ("16 bpc",       ptrSDR, 2))
    {
      _registry.regKVSDRMode.putData (_registry.iSDRMode);
      RecreateSwapChains = true;
    }
    ImGui::TreePop         ( );
    
    if ((_registry.iHDRMode > 0 && SKIF_Util_IsHDRActive ( )))
    {
      SKIF_ImGui_PopDisableState  ( );
    }

    ImGui::Spacing         ( );
    
    if (SKIF_Util_IsHDRSupported ( )  )
    {
      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      SKIF_ImGui_SetHoverTip ("Makes the app pop more on HDR displays.");
      ImGui::SameLine        ( );
      ImGui::TextColored (
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
          "High dynamic range (HDR):"
      );

      ImGui::TreePush        ("iHDRMode");

      if (_registry.iUIMode == 0)
      {
        ImGui::TextDisabled   ("HDR support is disabled while the UI is in Safe Mode.");
      }

      else if (SKIF_Util_IsHDRActive ( ))
      {
        if (ImGui::RadioButton ("No",             &_registry.iHDRMode, 0))
        {
          _registry.regKVHDRMode.putData (         _registry.iHDRMode);
          RecreateSwapChains = true;
        }
#ifdef SKIV_HDR10_SUPPORT
        ImGui::SameLine        ( );
        if (ImGui::RadioButton ("HDR10 (10 bpc)", &_registry.iHDRMode, 1))
        {
          _registry.regKVHDRMode.putData (         _registry.iHDRMode);
          RecreateSwapChains = true;
        }
#endif
        ImGui::SameLine        ( );
        if (ImGui::RadioButton ("scRGB (16 bpc)", &_registry.iHDRMode, 2))
        {
          _registry.regKVHDRMode.putData (         _registry.iHDRMode);
          RecreateSwapChains = true;
        }

        ImGui::Spacing         ( );

        // HDR Brightness

        if (_registry.iHDRMode == 0)
          SKIF_ImGui_PushDisableState ( );

        if (ImGui::GetContentRegionAvail().x > 725.0f)
          ImGui::SetNextItemWidth (500.0f);

        if (ImGui::SliderInt("HDR brightness", &_registry.iHDRBrightness, 80, 400, "%d nits"))
        {
          // Reset to 203 nits (default; HDR reference white for BT.2408) if negative or zero
          if (_registry.iHDRBrightness <= 0)
              _registry.iHDRBrightness  = 203;

          // Keep the nits value between 80 and 400
          _registry.iHDRBrightness = std::min (std::max (80, _registry.iHDRBrightness), 400);
          _registry.regKVHDRBrightness.putData (_registry.iHDRBrightness);
        }
    
        if (ImGui::IsItemActive    ( ))
          allowShortcutCtrlA = false;

        if (_registry.iHDRMode == 0)
          SKIF_ImGui_PopDisableState  ( );
      }

      else {
        ImGui::TextDisabled   ("Your display(s) supports HDR, but does not use it.");
      }

      if (SKIF_Util_GetHotKeyStateHDRToggle ( ) && _registry.iUIMode != 0)
      {
        ImGui::Spacing         ( );
        /*
        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped    ("FYI: Use " ICON_FA_WINDOWS " + Ctrl + Shift + H while this app is running to toggle "
                               "HDR for the display the mouse cursor is currently located on.");
        ImGui::PopStyleColor  ( );
        */
        
        ImGui::BeginGroup       ( );
        ImGui::TextDisabled     ("Use");
        ImGui::SameLine         ( );
        ImGui::TextColored      (
          ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),
          "Ctrl + " ICON_FA_WINDOWS " + Shift + H");
        ImGui::SameLine         ( );
        ImGui::TextDisabled     ("to toggle HDR where the");
        ImGui::SameLine         ( );
        ImGui::TextColored      (
          ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),
            ICON_FA_ARROW_POINTER "");
        ImGui::SameLine         ( );
        ImGui::TextDisabled     ("is.");
        ImGui::EndGroup         ( );
      }

      ImGui::TreePop         ( );
    }

#pragma endregion

    ImGui::PopStyleColor ( );
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

#pragma region Section: Advanced
  if (ImGui::CollapsingHeader ("Advanced###SKIF_SettingsHeader-3", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::PushStyleColor   (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                              );

    SKIF_ImGui_Spacing      ( );

    if (ImGui::Checkbox  ("Adjust window based on image size",
                                           &_registry.bAdjustWindow))
      _registry.regKVAdjustWindow.putData  (_registry.bAdjustWindow);

    if (! SKIF_Util_GetDragFromMaximized ( ))
      SKIF_ImGui_PushDisableState ( );

    if (ImGui::Checkbox  ("Maximize on double click",
                                                    &_registry.bMaximizeOnDoubleClick))
      _registry.regKVMaximizeOnDoubleClick.putData  (_registry.bMaximizeOnDoubleClick);
    
    if (! SKIF_Util_GetDragFromMaximized ( ))
    {
      SKIF_ImGui_PopDisableState ( );
      SKIF_ImGui_SetHoverTip ("Feature is inaccessible due to snapping and/or\n"
                              "drag from maximized being disabled in Windows.");
    }

    if ( ImGui::Checkbox (
            "Allow multiple instances of this app",
              &_registry.bMultipleInstances )
        )
    {
      if (! _registry.bMultipleInstances)
      {
        // Immediately close out any duplicate instances, they're undesirables
        EnumWindows ( []( HWND   hWnd,
                          LPARAM lParam ) -> BOOL
        {
          wchar_t                         wszRealWindowClass [64] = { };
          if (RealGetWindowClassW (hWnd,  wszRealWindowClass, 64))
          {
            if (StrCmpIW ((LPWSTR)lParam, wszRealWindowClass) == 0)
            {
              if (SKIF_Notify_hWnd != hWnd) // Don't send WM_QUIT to ourselves
                PostMessage (  hWnd, WM_QUIT,
                                0x0, 0x0  );
            }
          }
          return TRUE;
        }, (LPARAM)SKIF_NotifyIcoClass);
      }

      _registry.regKVMultipleInstances.putData (
        _registry.bMultipleInstances
        );
    }

    if ( ImGui::Checkbox ( "Always open this app on the same monitor as the mouse", &_registry.bOpenAtCursorPosition ) )
      _registry.regKVOpenAtCursorPosition.putData (                                  _registry.bOpenAtCursorPosition );

    if ( ImGui::Checkbox ( "Automatically install new updates",                     &_registry.bAutoUpdate ) )
      _registry.regKVAutoUpdate.putData (                                            _registry.bAutoUpdate);

#if 0
    if ( ImGui::Checkbox ( "Controller support",                                    &_registry.bControllers ) )
    {
      _registry.regKVControllers.putData (                                           _registry.bControllers);

      // Ensure the gamepad input thread knows what state we are actually in
      static SKIF_GamePadInputHelper& _gamepad =
             SKIF_GamePadInputHelper::GetInstance ( );

      if (_registry.bControllers)
        _gamepad.WakeThread  ( );
      else
        _gamepad.SleepThread ( );
    }

    ImGui::SameLine    ( );
    ImGui::TextColored      (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip  ("This enables the use of Xbox controllers to navigate within this app.");
#endif

    ImGui::Spacing         ( );

#pragma region Advanced::CheckForUpdates

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("This setting has no effect if low bandwidth mode is enabled.");
    ImGui::SameLine        ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Check for updates:"
    );

    ImGui::BeginGroup    ( );

    ImGui::TreePush        ("CheckForUpdates");
    if (ImGui::RadioButton ("Never",                 &_registry.iCheckForUpdates, 0))
      _registry.regKVCheckForUpdates.putData (         _registry.iCheckForUpdates);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Weekly",                &_registry.iCheckForUpdates, 1))
      _registry.regKVCheckForUpdates.putData (        _registry.iCheckForUpdates);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("On each launch",        &_registry.iCheckForUpdates, 2))
      _registry.regKVCheckForUpdates.putData (        _registry.iCheckForUpdates);

    ImGui::TreePop         ( );

    ImGui::EndGroup      ( );

    static SKIF_Updater& _updater = SKIF_Updater::GetInstance ( );

    bool disableCheckForUpdates = false;
    bool disableRollbackUpdates = false;

    if (_updater.IsRunning ( ))
      disableCheckForUpdates = disableRollbackUpdates = true;

    if (! disableCheckForUpdates && _updater.GetChannels ( )->empty( ))
      disableRollbackUpdates = true;

    ImGui::TreePush        ("UpdateChannels");

    ImGui::BeginGroup    ( );

    if (disableRollbackUpdates)
      SKIF_ImGui_PushDisableState ( );

    if (ImGui::GetContentRegionAvail().x > 725.0f)
      ImGui::SetNextItemWidth (500.0f);

    if (ImGui::BeginCombo ("###SKIF_wzUpdateChannel", _updater.GetChannel( )->second.c_str()))
    {
      for (auto& updateChannel : *_updater.GetChannels ( ))
      {
        bool is_selected = (_updater.GetChannel()->first == updateChannel.first);

        if (ImGui::Selectable (updateChannel.second.c_str(), is_selected) && updateChannel.first != _updater.GetChannel( )->first)
        {
          _updater.SetChannel (&updateChannel); // Update selection
          _updater.SetIgnoredUpdate (L"");      // Clear any ignored updates

          if (false)
            _updater.CheckForUpdates  ( );        // Trigger a new check for updates
        }

        if (is_selected)
          ImGui::SetItemDefaultFocus ( );
      }

      ImGui::EndCombo  ( );
    }

    if (disableRollbackUpdates)
      SKIF_ImGui_PopDisableState  ( );

    ImGui::SameLine        ( );

    if (disableCheckForUpdates)
      SKIF_ImGui_PushDisableState ( );
    else
      ImGui::PushStyleColor       (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success));

    if (ImGui::Button      (ICON_FA_ROTATE) && false)
    {
      _updater.SetIgnoredUpdate (L""); // Clear any ignored updates
      _updater.CheckForUpdates (true); // Trigger a forced check for updates/redownloads of repository.json and patrons.txt
    }

    SKIF_ImGui_SetHoverTip ("Check for updates");

    if (disableCheckForUpdates)
      SKIF_ImGui_PopDisableState  ( );
    else
      ImGui::PopStyleColor        ( );

    if (((_updater.GetState() & UpdateFlags_Older) == UpdateFlags_Older) || _updater.IsRollbackAvailable ( ))
    {
      ImGui::SameLine        ( );

      if (disableRollbackUpdates)
        SKIF_ImGui_PushDisableState ( );
      else
        ImGui::PushStyleColor       (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning));

      if (ImGui::Button      (ICON_FA_ROTATE_LEFT) &&false)
      {
        extern PopupState UpdatePromptPopup;

        // Ignore the current version
        _updater.SetIgnoredUpdate (SKIV_VERSION_STR_W); // TODO: Hardcoded -- needs to be changed later

        if ((_updater.GetState() & UpdateFlags_Older) == UpdateFlags_Older)
          UpdatePromptPopup = PopupState_Open;
        else
          _updater.CheckForUpdates (false, true); // Trigger a rollback
      }

      SKIF_ImGui_SetHoverTip ("Roll back to the previous version");

      if (disableRollbackUpdates)
        SKIF_ImGui_PopDisableState  ( );
      else
        ImGui::PopStyleColor        ( );
    }

    ImGui::EndGroup   ( );

    ImGui::TreePop    ( );

#pragma endregion

    ImGui::Spacing         ( );

#pragma region Advanced::Troubleshooting

    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        ICON_FA_WRENCH "  Troubleshooting:"
    );

    SKIF_ImGui_Spacing ( );

    ImGui::TreePush        ("TroubleshootingItems");

    const char* LogSeverity[] = { "None",
                                  "Fatal",
                                  "Error",
                                  "Warning",
                                  "Info",
                                  "Debug",
                                  "Verbose" };
    static const char* LogSeverityCurrent = LogSeverity[_registry.iLogging];

    if (ImGui::GetContentRegionAvail().x > 725.0f)
      ImGui::SetNextItemWidth (500.0f);

    if (ImGui::BeginCombo (" Log level###_registry.iLoggingCombo", LogSeverityCurrent))
    {
      for (int n = 0; n < IM_ARRAYSIZE (LogSeverity); n++)
      {
        bool is_selected = (LogSeverityCurrent == LogSeverity[n]);
        if (ImGui::Selectable (LogSeverity[n], is_selected))
        {
          _registry.iLogging = n;
          _registry.regKVLogging.putData  (_registry.iLogging);
          LogSeverityCurrent = LogSeverity[_registry.iLogging];
          plog::get()->setMaxSeverity((plog::Severity)_registry.iLogging);

          ImGui::GetCurrentContext()->DebugLogFlags = ImGuiDebugLogFlags_OutputToTTY | ((_registry.isDevLogging())
                                                    ? ImGuiDebugLogFlags_EventMask_
                                                    : ImGuiDebugLogFlags_EventViewport);
        }
        if (is_selected)
          ImGui::SetItemDefaultFocus ( );
      }
      ImGui::EndCombo  ( );
    }

    if (_registry.iLogging >= 6 && _registry.bDeveloperMode)
    {
      if (ImGui::Checkbox  ("Enable excessive development logging", &_registry.bLoggingDeveloper))
      {
        _registry.regKVLoggingDeveloper.putData                     (_registry.bLoggingDeveloper);

        ImGui::GetCurrentContext()->DebugLogFlags = ImGuiDebugLogFlags_OutputToTTY | ((_registry.isDevLogging())
                                                  ? ImGuiDebugLogFlags_EventMask_
                                                  : ImGuiDebugLogFlags_EventViewport);
      }
    }

    SKIF_ImGui_SetHoverTip  ("Only intended for SKIF developers as this enables excessive logging (e.g. window messages).");

    SKIF_ImGui_Spacing ( );

    const char* Diagnostics[] = { "None",
                                  "Normal",
                                  "Enhanced" };
    static const char* DiagnosticsCurrent = Diagnostics[_registry.iDiagnostics];

    if (ImGui::GetContentRegionAvail().x > 725.0f)
      ImGui::SetNextItemWidth (500.0f);

    if (ImGui::BeginCombo (" Diagnostics###_registry.iDiagnostics", DiagnosticsCurrent))
    {
      for (int n = 0; n < IM_ARRAYSIZE (Diagnostics); n++)
      {
        bool is_selected = (DiagnosticsCurrent == Diagnostics[n]);
        if (ImGui::Selectable (Diagnostics[n], is_selected))
        {
          _registry.iDiagnostics = n;
          _registry.regKVDiagnostics.putData (_registry.iDiagnostics);
          DiagnosticsCurrent = Diagnostics[_registry.iDiagnostics];
        }
        if (is_selected)
          ImGui::SetItemDefaultFocus ( );
      }
      ImGui::EndCombo  ( );
    }

    ImGui::SameLine    ( );
    ImGui::TextColored      (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip  ("Help improve Special K by allowing anonymized diagnostics to be sent.\n"
                             "The data is used to identify issues, highlight common use cases, and\n"
                             "facilitates the continued development of the application.");

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       ("https://wiki.special-k.info/Privacy");

    if (ImGui::IsItemClicked      ())
      SKIF_Util_OpenURI           (L"https://wiki.special-k.info/Privacy");

    SKIF_ImGui_Spacing ( );

    if (ImGui::Checkbox  ("Developer mode",  &_registry.bDeveloperMode))
    {
      _registry.regKVDeveloperMode.putData   (_registry.bDeveloperMode);

      ImGui::GetCurrentContext()->DebugLogFlags = ImGuiDebugLogFlags_OutputToTTY | ((_registry.isDevLogging())
                                                ? ImGuiDebugLogFlags_EventMask_
                                                : ImGuiDebugLogFlags_EventViewport);
    }

    SKIF_ImGui_SetHoverTip  ("Exposes additional information and context menu items that may be of interest for developers.");

    ImGui::SameLine    ( );

    if (ImGui::Checkbox  ("Efficiency mode", &_registry.bEfficiencyMode))
      _registry.regKVEfficiencyMode.putData  (_registry.bEfficiencyMode);

    SKIF_ImGui_SetHoverTip  ("Engage efficiency mode for this app when idle.\n"
                             "Not recommended for Windows 10 and earlier.");

    static std::wstring wsPathToSKDll = SK_FormatStringW (
        LR"(%ws\%ws)",
          _path_cache.specialk_userdata, // Can theoretically be wrong
#ifdef _WIN64
          L"SpecialK64.dll"
#else
          L"SpecialK32.dll"
#endif
        );

    static std::wstring wsDisableCall = SK_FormatStringW (
      LR"("%ws\%ws",RunDLL_DisableGFEForSKIF)",
        _path_cache.specialk_userdata, // Can theoretically be wrong
#ifdef _WIN64
        L"SpecialK64.dll"
#else
        L"SpecialK32.dll"
#endif
      );

    static bool bPathToSkDLL =
      PathFileExists (wsPathToSKDll.c_str());

    // Only show if the Special K DLL file could actually be found
    if (bPathToSkDLL)
    {
      SKIF_ImGui_Spacing ( );

      ImGui::TextWrapped ("Nvidia users: Use the below button to prevent GeForce Experience from mistaking this app for a game.");

      ImGui::Spacing     ( );

      static bool runOnceGFE = false;

      if (runOnceGFE)
        SKIF_ImGui_PushDisableState ( );

      if (ImGui::ButtonEx (ICON_FA_USER_SHIELD " Disable GFE notifications",
                                   ImVec2 (250 * SKIF_ImGui_GlobalDPIScale,
                                            25 * SKIF_ImGui_GlobalDPIScale)))
      {
        runOnceGFE = true;

        PLOG_INFO << "Attempting to disable GeForce Experience / ShadowPlay notifications...";
        wchar_t              wszRunDLL32 [MAX_PATH + 2] = { };
        GetSystemDirectoryW (wszRunDLL32, MAX_PATH);
        PathAppendW         (wszRunDLL32, L"rundll32.exe");

        SHELLEXECUTEINFOW
          sexi              = { };
          sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
          sexi.lpVerb       = L"RUNAS";
          sexi.lpFile       = wszRunDLL32;
        //sexi.lpDirectory  = ;
          sexi.lpParameters = wsDisableCall.c_str();
          sexi.nShow        = SW_SHOWNORMAL;
          sexi.fMask        = SEE_MASK_NOASYNC | SEE_MASK_NOZONECHECKS;
        
        SetLastError (NO_ERROR);

        bool ret = ShellExecuteExW (&sexi);

        if (GetLastError ( ) != NO_ERROR)
          PLOG_ERROR << "An unexpected error occurred: " << SKIF_Util_GetErrorAsWStr();

        if (ret)
          PLOG_INFO  << "The operation was successful.";
        else
          PLOG_ERROR << "The operation was unsuccessful.";
      }
    
      // Prevent this call from executing on the same frame as the button is pressed
      else if (runOnceGFE)
        SKIF_ImGui_PopDisableState ( );
    
      ImGui::SameLine         ( );
      ImGui::TextColored      (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      SKIF_ImGui_SetHoverTip  ("This only needs to be used if GeForce Experience notifications\n"
                               "appear on the screen whenever this app is being used.");
    }

    ImGui::TreePop ( );

#pragma endregion


    ImGui::PopStyleColor    ( );
  }

  SKIF_ImGui_Spacing ( );
  SKIF_ImGui_Spacing ( );
  
  if (ImGui::Button (ICON_FA_LEFT_LONG " Go back###GoBackBtn2", ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale, 30.0f * SKIF_ImGui_GlobalDPIScale)))
    SKIF_Tab_ChangeTo = UITab_Viewer;

  ImGui::Spacing ();
  ImGui::Spacing ();

#pragma endregion

}
