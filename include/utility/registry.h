#pragma once
#include <string>
#include <windows.h>
#include <typeindex>
#include <sstream>
#include <vector>
#include "sk_utility.h"

#ifndef RRF_SUBKEY_WOW6464KEY
#define RRF_SUBKEY_WOW6464KEY  0x00010000
#endif // !RRF_SUBKEY_WOW6464KEY

#ifndef RRF_SUBKEY_WOW6432KEY
#define RRF_SUBKEY_WOW6432KEY  0x00020000
#endif // !RRF_SUBKEY_WOW6432KEY

#ifndef RRF_WOW64_MASK
#define RRF_WOW64_MASK         0x00030000
#endif // !RRF_WOW64_MASK

struct SKIF_RegistrySettings {

  // TODO: Rework this whole thing to not only hold a registry path but
  //       also hold the actual current value as well, allowing us to
  //       move away from ugly stuff like
  // 
  //  _registry.uiLastSelectedGame = newValue;
  //  _registry.regKVLastSelectedGame.putData  (_registry.uiLastSelectedGame);
  // 
  //       and instead do things like
  // 
  //  _registry.uiLastSelectedGame.putData (newValue);
  // 
  //       and have it automatically get stored in the registry as well.

  template <class _Tp>
    class KeyValue
    {
      struct KeyDesc {
        HKEY         hKey                 = HKEY_CURRENT_USER;
        wchar_t    wszSubKey   [MAX_PATH] =               { };
        wchar_t    wszKeyValue [MAX_PATH] =               { };
        DWORD        dwType               =          REG_NONE;
        DWORD        dwFlags              =        RRF_RT_ANY;
      };

    public:
      bool         hasData        (HKEY* hKey = nullptr);
      _Tp          getData        (HKEY* hKey = nullptr);
      bool         putDataMultiSZ (std::vector<std::wstring> in);
      bool         putData        (_Tp in)
      {
        if ( ERROR_SUCCESS == _SetValue (&in) )
          return true;

        return false;
      };

      static KeyValue <typename _Tp>
         MakeKeyValue ( const wchar_t *wszSubKey,
                        const wchar_t *wszKeyValue,
                        HKEY           hKey    = HKEY_CURRENT_USER,
                        LPDWORD        pdwType = nullptr,
                        DWORD          dwFlags = RRF_RT_ANY );

    protected:
    private:
      KeyDesc _desc;
      
      LSTATUS _SetValue (_Tp * pVal)
      {
        LSTATUS lStat         = STATUS_INVALID_DISPOSITION;
        HKEY    hKeyToSet     = 0;
        DWORD   dwDisposition = 0;
        DWORD   dwDataSize    = 0;

        lStat =
          RegCreateKeyExW (
            _desc.hKey,
              _desc.wszSubKey,
                0x00, nullptr,
                  REG_OPTION_NON_VOLATILE,
                  KEY_ALL_ACCESS, nullptr,
                    &hKeyToSet, &dwDisposition );

        auto type_idx =
          std::type_index (typeid (_Tp));

        if ( type_idx == std::type_index (typeid (std::wstring)) )
        {
          std::wstring _in = std::wstringstream(*pVal).str();

          _desc.dwType     = REG_SZ;
                dwDataSize = (DWORD) _in.size ( ) * sizeof(wchar_t);

          lStat =
            RegSetKeyValueW ( hKeyToSet,
                                nullptr,
                                _desc.wszKeyValue,
                                _desc.dwType,
                          (LPBYTE) _in.data(), dwDataSize);
            
          RegCloseKey (hKeyToSet);

          return lStat;
        }

        if ( type_idx == std::type_index (typeid (bool)) )
        {
          _desc.dwType     = REG_BINARY;
                dwDataSize = sizeof (bool);
        }

        if ( type_idx == std::type_index (typeid (int)) )
        {
          _desc.dwType     = REG_DWORD;
                dwDataSize = sizeof (int);
        }

        if ( type_idx == std::type_index (typeid (float)) )
        {
          _desc.dwFlags    = RRF_RT_DWORD;
          _desc.dwType     = REG_BINARY;
                dwDataSize = sizeof (float);
        }

        lStat =
          RegSetKeyValueW ( hKeyToSet,
                              nullptr,
                              _desc.wszKeyValue,
                              _desc.dwType,
                                pVal, dwDataSize );

        RegCloseKey (hKeyToSet);

        return lStat;
      };
      
      LSTATUS _GetValue (_Tp* pVal, DWORD* pLen = nullptr, HKEY* hKey = nullptr)
      {
        LSTATUS lStat =
          RegGetValueW ( (hKey != nullptr) ? *hKey : _desc.hKey,
                         (hKey != nullptr) ?  NULL : _desc.wszSubKey,
                              _desc.wszKeyValue,
                              _desc.dwFlags,
                                &_desc.dwType,
                                  pVal, pLen );

        return lStat;
      };

      DWORD _SizeOfData (HKEY* hKey = nullptr)
      {
        DWORD len = 0;

        if ( ERROR_SUCCESS ==
                _GetValue ( nullptr, &len, hKey)
            ) return len;

        return 0;
      };
  };

#define SKIF_MakeRegKeyF   KeyValue <float>       ::MakeKeyValue
#define SKIF_MakeRegKeyB   KeyValue <bool>        ::MakeKeyValue
#define SKIF_MakeRegKeyI   KeyValue <int>         ::MakeKeyValue
#define SKIF_MakeRegKeyWS  KeyValue <std::wstring>::MakeKeyValue
#define SKIF_MakeRegKeyVEC KeyValue <std::vector <std::wstring>>::MakeKeyValue
  
  // Booleans

  KeyValue <bool> regKVUIBorders =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Borders)" );

  KeyValue <bool> regKVUITooltips =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Tooltips)" );

  KeyValue <bool> regKVUIStatusBar =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Status Bar)" );

  KeyValue <bool> regKVDPIScaling =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(DPI Scaling)" );

  KeyValue <bool> regKVWin11Corners =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Win11 Corners)" );

  KeyValue <bool> regKVUICaptionButtons =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(UI Caption Buttons)" );

  KeyValue <bool> regKVTouchInput =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(UI Touch Input)" );

  KeyValue <bool> regKVFirstLaunch =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(First Launch)" );

  KeyValue <bool> regKVCloseToTray =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Close To Notification Area)" );

  KeyValue <bool> regKVMultipleInstances =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Multiple Instances)" );

  KeyValue <bool> regKVOpenAtCursorPosition =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Open At Cursor Position)" );

  KeyValue <bool> regKVAlwaysShowGhost =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Always Show Ghost)" );

  KeyValue <bool> regKVMaximizeOnDoubleClick =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Maximize On Double Click)" );

  KeyValue <bool> regKVAutoUpdate =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Auto-Update)" );

  KeyValue <bool> regKVDeveloperMode =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Developer Mode)" );

  KeyValue <bool> regKVEfficiencyMode =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Efficiency Mode)" );

  KeyValue <bool> regKVFadeCovers =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Fade Covers)" );

  KeyValue <bool> regKVControllers =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Controllers)" );

  KeyValue <bool> regKVLoggingDeveloper =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Logging Developer)" );

  KeyValue <bool> regKVNotifications =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Notifications)" );

  KeyValue <bool> regKVGhost =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Ghost)" );

  KeyValue <bool> regKVAdjustWindow =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Resize Window)" );

  KeyValue <bool> regKVImageDetails =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Image Details)" );

  // Integers (DWORDs)

  KeyValue <int> regKVImageScaling =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Scaling)" );

  KeyValue <int> regKVStyle =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Style)" );

  KeyValue <int> regKVLogging =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Logging)" );

  KeyValue <int> regKVDarkenImages =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Darken)" );

  KeyValue <int> regKVCheckForUpdates =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Check For Updates)" );

  KeyValue <int> regKVSDRMode =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(SDR)" );

  KeyValue <int> regKVHDRMode =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(HDR)" );

  KeyValue <int> regKVHDRBrightness =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(HDR Brightness)" );

  KeyValue <int> regKVUIMode =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(UI Mode)" );

  KeyValue <int> regKVDiagnostics =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Diagnostics)" );

  // Wide Strings

  KeyValue <std::wstring> regKVIgnoreUpdate =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Ignore Update)" );

  KeyValue <std::wstring> regKVUpdateChannel =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Update Channel)" );

  KeyValue <std::wstring> regKVPathViewer =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Path)" );

  KeyValue <std::wstring> regKVAutoUpdateVersion =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Auto-Update Version)" );

  KeyValue <std::wstring> regKVHotkeyCaptureRegion =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Hotkey Capture Region)" );

  // Multi wide Strings

  KeyValue <std::vector<std::wstring>> regKVCategories =
    SKIF_MakeRegKeyVEC ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Categories)" );

  KeyValue <std::vector<std::wstring>> regKVCategoriesState =
    SKIF_MakeRegKeyVEC ( LR"(SOFTWARE\Kaldaien\Special K\Viewer\)",
                         LR"(Categories State)" );

  // Special K stuff

  KeyValue <std::wstring> regKVPathSpecialK =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Path)" );

  // Windows stuff

  // App registration
  KeyValue <std::wstring> regKVAppRegistration =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\SKIV.exe)",
                         L"" ); // Default value

  // Notification duration
  KeyValue <int> regKVNotificationsDuration =
    SKIF_MakeRegKeyI  ( LR"(Control Panel\Accessibility\)",
                         LR"(MessageDuration)" );

  // Light theme
  KeyValue <int> regKVWindowUseLightTheme =
    SKIF_MakeRegKeyI  ( LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\)",
                         LR"(AppsUseLightTheme)" );

  // Default settings (multiple options)
//int iImageScaling            = 2;   // 0 = None,                        1 = Fill,                   2 = Fit (default),               3 = Stretch
  int iStyle                   = 0;   // 0 = Dynamic,                     1 = SKIF Dark,              2 = SKIF Light,                  3 = ImGui Classic,                  4 = ImGui Dark
  int iStyleTemp               = 0;   // Used to temporary hold changes in the style during the current session
  int iDarkenImages            = 0;   // 0 = Never,                       1 = Always,                 2 = On mouse hover
  int iCheckForUpdates         = 1;   // 0 = Never,                       1 = Weekly,                 2 = On each launch
  int iLogging                 = 4;   // 0 = None,                        1 = Fatal,                  2 = Error,                       3 = Warning,                        4 = Info,       5 = Debug,       6 = Verbose
  int iSDRMode                 = 0;   // 0 = 8 bpc,                       1 = 10 bpc,                 2 = 16 bpc
  int iHDRMode                 = 2;   // 0 = Disabled,                    1 = HDR10 (10 bpc),         2 = scRGB (16 bpc)
  int iHDRBrightness           = 203; // HDR reference white for BT.2408
  int iUIMode                  = 1;   // 0 = Safe Mode (BitBlt),          1 = Normal,                 2 = VRR Compatibility
  int iDiagnostics             = 1;   // 0 = None,                        1 = Normal,                 2 = Enhanced (not actually used yet)

  // Default settings (booleans)
  bool bAdjustWindow            = false; // Adjust window size based on the image size?
  bool bGhost                   = false; // Visibility of Shelly the Ghost
  bool bNotifications           =  true;
  bool bUIBorders               = false;
  bool bUITooltips              =  true;
  bool bUIStatusBar             =  true;
  bool bUICaptionButtons        = false; // Minimize, Close
  bool bDPIScaling              =  true;
  bool bWin11Corners            =  true; // 2023-08-28: Enabled by default
  bool bTouchInput              =  true; // Automatically make the UI more optimized for touch input on capable devices
  bool bImageDetails            = false;

  bool bFirstLaunch             = false;
  bool bCloseToTray             = false;
  bool bMultipleInstances       = false;
  bool bOpenAtCursorPosition    = false;
#if 0
  bool bMaximizeOnDoubleClick   =  true;
#endif
  bool bAutoUpdate              = false; // Automatically runs downloaded installers
  bool bDeveloperMode           = false;
  bool bEfficiencyMode          =  true; // Should the main thread try to engage EcoQoS / Efficiency Mode on Windows 11 ?
  bool bFadeCovers              =  true;
  bool bControllers             =  true; // Should SKIF support controller input ?
  bool bLoggingDeveloper        = false; // This is a log level "above" verbose logging that also includes stuff like window messages. Only useable for SKIF developers

  // Wide strings
  std::wstring wsUpdateChannel  = L"Website"; // Default to stable channel
  std::wstring wsIgnoreUpdate;
  std::wstring wsPathViewer;
  std::wstring wsPathSpecialK;
  std::wstring wsAutoUpdateVersion; // Holds the version the auto-updater is trying to install
  std::wstring wsDefaultHDRExt = L".png";
  std::wstring wsDefaultSDRExt = L".png";

  // Windows stuff
  std::wstring wsAppRegistration;
  int  iNotificationsDuration       = 5; // Defaults to 5 seconds in case Windows is not set to something else

  // Ephemeral settings that doesn't stick around
  bool _sRGBColors                  = false;
  bool _EfficiencyMode              = false;
  bool _StyleLightMode              = false; // Indicates whether we are currently using a light theme or not
  bool _RendererCanWaitSwapchain    = false; // Waitable Swapchain            Windows 8.1+
  bool _RendererCanAllowTearing     = false; // DWM Tearing                   Windows 10+
  bool _RendererCanHDR              = false; // High Dynamic Range            Windows 10 1709+ (Build 16299)
  bool _RendererHDREnabled          = false; // HDR Enabled
  bool _TouchDevice                 = false;
  bool _SnippingMode                = false;
  bool _SnippingModeExit            = false;

  // Keybindings

  SK_Keybind kbCaptureRegion = SK_Keybind {
        "Capture Region",
       L"Ctrl+Windows+Shift+P",
        "Ctrl+Windows+Shift+P"
  };

  // Functions
  bool isDevLogging (void) const;

  static SKIF_RegistrySettings& GetInstance (void)
  {
      static SKIF_RegistrySettings instance;
      return instance;
  }

  SKIF_RegistrySettings (SKIF_RegistrySettings const&) = delete; // Delete copy constructor
  SKIF_RegistrySettings (SKIF_RegistrySettings&&)      = delete; // Delete move constructor

private:
  SKIF_RegistrySettings (void);
};
