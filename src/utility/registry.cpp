#include <utility/registry.h>
#include <algorithm>
#include <utility/sk_utility.h>
#include <utility/utility.h>

extern bool SKIF_Util_IsWindows10OrGreater      (void);
extern bool SKIF_Util_IsWindows10v1709OrGreater (void);

template<class _Tp>
bool
SKIF_RegistrySettings::KeyValue<_Tp>::hasData (HKEY* hKey)
{
  _Tp   out = _Tp ( );
  DWORD dwOutLen;

  auto type_idx =
    std::type_index (typeid (_Tp));;

  if ( type_idx == std::type_index (typeid (std::wstring)) )
  {
    _desc.dwFlags  = RRF_RT_REG_SZ;
    _desc.dwType   = REG_SZ;

    // Two null terminators are stored at the end of REG_SZ, so account for those
    return (_SizeOfData (hKey) > 4);
  }

  if ( type_idx == std::type_index (typeid (bool)) )
  {
    _desc.dwType   = REG_BINARY;
          dwOutLen = sizeof (bool);
  }

  if ( type_idx == std::type_index (typeid (int)) )
  {
    _desc.dwType   = REG_DWORD;
          dwOutLen = sizeof (int);
  }

  if ( type_idx == std::type_index (typeid (float)) )
  {
    _desc.dwFlags  = RRF_RT_REG_BINARY;
    _desc.dwType   = REG_BINARY;
          dwOutLen = sizeof (float);
  }

  if ( ERROR_SUCCESS == _GetValue (&out, &dwOutLen, hKey) )
    return true;

  return false;
}

std::vector <std::wstring>
SKIF_RegistrySettings::KeyValue<std::vector <std::wstring>>::getData (HKEY* hKey)
{
  _desc.dwFlags  = RRF_RT_REG_MULTI_SZ;
  _desc.dwType   = REG_MULTI_SZ;
  DWORD dwOutLen = _SizeOfData (hKey);

  std::wstring out(dwOutLen, '\0');

  if ( ERROR_SUCCESS != 
    RegGetValueW ( (hKey != nullptr) ? *hKey : _desc.hKey,
                   (hKey != nullptr) ?  NULL : _desc.wszSubKey,
                        _desc.wszKeyValue,
                        _desc.dwFlags,
                          &_desc.dwType,
                            out.data(), &dwOutLen)) return std::vector <std::wstring>();

  std::vector <std::wstring> vector;

  const wchar_t* currentItem = (const wchar_t*)out.data();

  // Parse the given wstring into a vector
  while (*currentItem)
  {
    vector.push_back (currentItem);
    currentItem = currentItem + _tcslen(currentItem) + 1;
  }

  /*
  // Strip null terminators
  for (auto& item : vector)
  {
    item.erase (std::find (item.begin(), item.end(), '\0'), item.end());
    OutputDebugStringW (L"Found: ");
    OutputDebugStringW (item.c_str());
    OutputDebugStringW (L"\n");
  }
  */

  return vector;
}

std::wstring
SKIF_RegistrySettings::KeyValue<std::wstring>::getData (HKEY* hKey)
{
  _desc.dwFlags  = RRF_RT_REG_SZ;
  _desc.dwType   = REG_SZ;
  DWORD dwOutLen = _SizeOfData (hKey);

  std::wstring out(dwOutLen, '\0');

  if ( ERROR_SUCCESS != 
    RegGetValueW ( (hKey != nullptr) ? *hKey : _desc.hKey,
                   (hKey != nullptr) ?  NULL : _desc.wszSubKey,
                        _desc.wszKeyValue,
                        _desc.dwFlags,
                          &_desc.dwType,
                            out.data(), &dwOutLen)) return std::wstring();

  // Strip null terminators
  out.erase (std::find (out.begin(), out.end(), '\0'), out.end());

  return out;
}

bool
SKIF_RegistrySettings::KeyValue<std::vector <std::wstring>>::putDataMultiSZ (std::vector<std::wstring> _in)
{
  LSTATUS lStat         = STATUS_INVALID_DISPOSITION;
  HKEY    hKeyToSet     = 0;
  DWORD   dwDisposition = 0;
  size_t  stDataSize    = 0;

  lStat =
    RegCreateKeyExW (
      _desc.hKey,
        _desc.wszSubKey,
          0x00, nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_ALL_ACCESS, nullptr,
              &hKeyToSet, &dwDisposition );

  _desc.dwType     = REG_MULTI_SZ;

  std::wstring wzData;

  // Serialize into std::wstring
  for (const auto& item : _in)
  {
    wzData     += item + L'\0';
    stDataSize += item.length ( ) + 1;
  }

  wzData    += L'\0';
  stDataSize++;

  lStat =
    RegSetKeyValueW ( hKeyToSet,
                        nullptr,
                        _desc.wszKeyValue,
                        _desc.dwType,
                  (LPBYTE) wzData.data ( ), (DWORD) stDataSize * sizeof(wchar_t));
            
  RegCloseKey (hKeyToSet);

  return (ERROR_SUCCESS == lStat);
}

template<class _Tp>
_Tp
SKIF_RegistrySettings::KeyValue<_Tp>::getData (HKEY* hKey)
{
  _Tp   out = _Tp ( );
  DWORD dwOutLen;

  auto type_idx =
    std::type_index (typeid (_Tp));

  if ( type_idx == std::type_index (typeid (bool)) )
  {
    _desc.dwType   = REG_BINARY;
          dwOutLen = sizeof (bool);
  }

  if ( type_idx == std::type_index (typeid (int)) )
  {
    _desc.dwType   = REG_DWORD;
          dwOutLen = sizeof (int);
  }

  if ( type_idx == std::type_index (typeid (float)) )
  {
    _desc.dwFlags  = RRF_RT_REG_BINARY;
    _desc.dwType   = REG_BINARY;
          dwOutLen = sizeof (float);
  }

  if ( ERROR_SUCCESS !=
          _GetValue (&out, &dwOutLen, hKey) ) out = _Tp ();

  return out;
}

template<class _Tp>
SKIF_RegistrySettings::KeyValue<_Tp>
SKIF_RegistrySettings::KeyValue<_Tp>::MakeKeyValue (const wchar_t* wszSubKey, const wchar_t* wszKeyValue, HKEY hKey, LPDWORD pdwType, DWORD dwFlags)
{
  KeyValue <_Tp> kv;

  wcsncpy_s ( kv._desc.wszSubKey,   MAX_PATH,
                        wszSubKey, _TRUNCATE );

  wcsncpy_s ( kv._desc.wszKeyValue,   MAX_PATH,
                        wszKeyValue, _TRUNCATE );

  kv._desc.hKey    = hKey;
  kv._desc.dwType  = ( pdwType != nullptr ) ?
                                    *pdwType : REG_NONE;
  kv._desc.dwFlags = dwFlags;

  return kv;
}

bool
SKIF_RegistrySettings::isDevLogging (void) const
{
  return (bLoggingDeveloper && bDeveloperMode && iLogging >= 6);
}

SKIF_RegistrySettings::SKIF_RegistrySettings (void)
{
  // iSDRMode defaults to 0, meaning 8 bpc (DXGI_FORMAT_R8G8B8A8_UNORM) 
  // but it seems that Windows 10 1709+ (Build 16299) also supports
  // 10 bpc (DXGI_FORMAT_R10G10B10A2_UNORM) for flip model.
  if (SKIF_Util_IsWindows10v1709OrGreater ( ))
    iSDRMode               =   1; // Default to 10 bpc on Win10 1709+

  // iUIMode defaults to 1 on Win7 and 8.1, but 2 on 10+
  if (SKIF_Util_IsWindows10OrGreater ( ))
    iUIMode                =   2;

  HKEY hKey = nullptr;
  
  LSTATUS lsKey = RegCreateKeyW (HKEY_CURRENT_USER, LR"(SOFTWARE\Kaldaien\Special K\Viewer\)", &hKey);

  if (lsKey != ERROR_SUCCESS)
    hKey = nullptr;
  
  // UI elements that can be toggled

  if (regKVUIBorders.hasData(&hKey))
    bUIBorders             =   regKVUIBorders              .getData (&hKey);
  if (regKVUITooltips.hasData(&hKey))
    bUITooltips            =   regKVUITooltips             .getData (&hKey);
  if (regKVUIStatusBar.hasData(&hKey))
    bUIStatusBar           =   regKVUIStatusBar            .getData (&hKey);
  if (regKVDPIScaling.hasData(&hKey))
    bDPIScaling            =   regKVDPIScaling             .getData (&hKey);
  if (regKVWin11Corners.hasData(&hKey))
    bWin11Corners          =   regKVWin11Corners           .getData (&hKey);
  if (regKVUICaptionButtons.hasData(&hKey))
    bUICaptionButtons      =   regKVUICaptionButtons       .getData (&hKey);
  if (regKVTouchInput.hasData(&hKey))
    bTouchInput            =   regKVTouchInput             .getData (&hKey);
  if (regKVAdjustWindow.hasData(&hKey))
    bAdjustWindow          =   regKVAdjustWindow           .getData (&hKey);

  if (regKVImageScaling.hasData(&hKey))
    iImageScaling          =   regKVImageScaling           .getData (&hKey);
  
  if (regKVSDRMode.hasData(&hKey))
    iSDRMode               =   regKVSDRMode                .getData (&hKey);

  if (regKVHDRMode.hasData(&hKey))
    iHDRMode               =   regKVHDRMode                .getData (&hKey);

  // HDR10 is not available for SKIV
  if (iHDRMode == 1)
    iHDRMode = 2;

  if (regKVHDRBrightness.hasData(&hKey))
  {
    iHDRBrightness         =   regKVHDRBrightness          .getData (&hKey);
    
    // Reset to 203 nits (the default) if outside of the acceptable range of 80-400 nits
    if (iHDRBrightness < 80 || 400 < iHDRBrightness)
      iHDRBrightness       =   203;
  }
  
  if (regKVUIMode.hasData(&hKey))
    iUIMode                =   regKVUIMode                 .getData (&hKey);
  
  if (regKVDiagnostics.hasData(&hKey))
    iDiagnostics           =   regKVDiagnostics            .getData (&hKey);

  if (! SKIF_Util_GetDragFromMaximized ( ))
    bMaximizeOnDoubleClick = false; // Force disabled IF the OS prerequisites are not enabled

  else if (regKVMaximizeOnDoubleClick.hasData(&hKey))
    bMaximizeOnDoubleClick = regKVMaximizeOnDoubleClick    .getData (&hKey);

  if (regKVNotifications.hasData(&hKey))
    bNotifications         =   regKVNotifications          .getData (&hKey);

  if (regKVStyle.hasData(&hKey))
    iStyle  =  iStyleTemp  =   regKVStyle                  .getData (&hKey);

  if (regKVLogging.hasData(&hKey))
    iLogging               =   regKVLogging                .getData (&hKey);

  if (regKVDarkenImages.hasData(&hKey))
    iDarkenImages          =   regKVDarkenImages           .getData (&hKey);

  if (regKVCheckForUpdates.hasData(&hKey))
    iCheckForUpdates       =   regKVCheckForUpdates        .getData (&hKey);

  if (regKVIgnoreUpdate.hasData(&hKey))
    wsIgnoreUpdate         =   regKVIgnoreUpdate           .getData (&hKey);

  if (regKVUpdateChannel.hasData(&hKey))
    wsUpdateChannel        =   regKVUpdateChannel          .getData (&hKey);

  if (regKVPathViewer.hasData(&hKey))
    wsPathViewer           =   regKVPathViewer             .getData (&hKey);

  if (regKVAutoUpdateVersion.hasData(&hKey))
    wsAutoUpdateVersion    =   regKVAutoUpdateVersion      .getData (&hKey);

  bDeveloperMode           =   regKVDeveloperMode          .getData (&hKey);

  if (regKVEfficiencyMode.hasData(&hKey))
    bEfficiencyMode        =   regKVEfficiencyMode         .getData (&hKey);
  else
    bEfficiencyMode        =   SKIF_Util_IsWindows11orGreater ( ); // Win10 and below: false, Win11 and above: true
  
  if (regKVFadeCovers.hasData(&hKey))
    bFadeCovers            =   regKVFadeCovers             .getData (&hKey);

  if (regKVControllers.hasData(&hKey))
    bControllers           =   regKVControllers            .getData (&hKey);

  // These defaults to false, so no need to check if the registry has another value
  //   since getData ( ) defaults to false for non-existent registry values
  bFirstLaunch             =   regKVFirstLaunch            .getData (&hKey);
  bMultipleInstances       =   regKVMultipleInstances      .getData (&hKey);
  bAutoUpdate              =   regKVAutoUpdate             .getData (&hKey);
  bOpenAtCursorPosition    =   regKVOpenAtCursorPosition   .getData (&hKey);
  bGhost                   =   regKVGhost                  .getData (&hKey);
  bLoggingDeveloper        =   regKVLoggingDeveloper       .getData (&hKey);

  if (hKey != nullptr)
    RegCloseKey (hKey);

  // Special K stuff

  if (regKVPathSpecialK.hasData())
    wsPathSpecialK         =   regKVPathSpecialK           .getData ( );

  // Windows stuff

  // App registration
  if (regKVAppRegistration.hasData())
    wsAppRegistration      =   regKVAppRegistration        .getData ( );

  // Notification duration
  if (regKVNotificationsDuration.hasData())
    iNotificationsDuration =   regKVNotificationsDuration  .getData ( );
  iNotificationsDuration *= 1000; // Convert from seconds to milliseconds
}