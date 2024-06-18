//
// Copyright 2020 - 2022 Andon "Kaldaien" Coleman
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

#include "../resource.h"
#include "../version.h"

#include <strsafe.h>
#include <cwctype>
#include <dxgi1_5.h>

#include <SKIV.h>

// Plog ini includes (must be included after SKIF.h)
#include "plog/Initializers/RollingFileInitializer.h"
#include "plog/Appenders/ConsoleAppender.h"
#include "plog/Appenders/DebugOutputAppender.h"
#include <utility/plog_formatter.h>

#include <utility/utility.h>
#include <utility/skif_imgui.h>
#include <utility/gamepad.h>

#include <fonts/fa_621.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include "imgui/imgui_impl_dx11.h"
#include <imgui/imgui_internal.h>
#include <ImGuiNotify.hpp>

#include <utility/fsutil.h>
#include <xinput.h>

#include <filesystem>
#include <concurrent_queue.h>
#include <oleidl.h>
#include <utility/droptarget.hpp>

#include <d3d11.h>
#define DIRECTINPUT_VERSION 0x0800

#include <tabs/viewer.h>
#include <tabs/settings.h>

#include <utility/registry.h>
#include <utility/updater.h>

#include <tabs/common_ui.h>
#include <Dbt.h>

// Header Files for Jump List features
#include <objectarray.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <netlistmgr.h>
#include <html_coder.hpp>

const int SKIF_STEAM_APPID      = 1157970;
bool  RecreateSwapChains        = false;
bool  RecreateSwapChainsPending = false;
bool  RecreateWin32Windows      = false;
bool  RepositionSKIF            = false;
bool  RespectMonBoundaries      = false;
bool  changedHiDPIScaling       = false;
bool  invalidateFonts           = false;
bool  failedLoadFonts           = false;
bool  failedLoadFontsPrompt     = false;
DWORD invalidatedFonts          = 0;
DWORD invalidatedDevice         = 0;
bool  startedMinimized          = false;
bool  msgDontRedraw             = false;
bool  imageFadeActive           = false;
std::atomic<bool> SKIF_Shutdown = false;
bool  SKIF_NoInternet           = false;
int   SKIF_ExitCode             = 0;
int   SKIF_nCmdShow             = -1;
std::atomic<int>  SKIF_FrameCount = 0;
int   addAdditionalFrames       = 0;
DWORD dwDwmPeriod               = 16; // Assume 60 Hz by default
bool  SteamOverlayDisabled      = false;
bool  allowShortcutCtrlA        = true; // Used to disable the Ctrl+A when interacting with text input
bool  SKIF_MouseDragMoveAllowed = true;
bool  SKIF_debuggerPresent      = false;
DWORD SKIF_startupTime          = 0; // Used as a basis of how long the initialization took
DWORD SKIF_firstFrameTime       = 0; // Used as a basis of how long the initialization took
HANDLE SteamProcessHandle       = NULL;

// Shell messages (registered window messages)
UINT SHELL_TASKBAR_RESTART        = 0; // TaskbarCreated
UINT SHELL_TASKBAR_BUTTON_CREATED = 0; // TaskbarButtonCreated

// --- App Mode (regular)
ImVec2 SKIF_vecRegularMode          = ImVec2 (0.0f, 0.0f);
ImVec2 SKIF_vecRegularModeDefault   = ImVec2 (1000.0f, 944.0f);   // Does not include the status bar
ImVec2 SKIF_vecRegularModeAdjusted  = SKIF_vecRegularModeDefault; // Adjusted for status bar and tooltips (NO DPI scaling!)
// --- Variables
ImVec2 SKIF_vecCurrentMode          = ImVec2 (0.0f, 0.0f); // Gets updated after ImGui::EndFrame()
ImVec2 SKIF_vecCurrentModeNext      = ImVec2 (0.0f, 0.0f); // Holds the new expected size
ImVec2 SKIF_vecAlteredSize          = ImVec2 (0.0f, 0.0f);
float  SKIF_fStatusBarHeight        = 31.0f; // Status bar enabled
float  SKIF_fStatusBarDisabled      = 8.0f;  // Status bar disabled
float  SKIF_fStatusBarHeightTips    = 18.0f; // Disabled tooltips (two-line status bar)
ImVec2 SKIV_ResizeApp               = ImVec2 (0.0f, 0.0f);

// Custom Global Key States used for moving SKIF around using WinKey + Arrows
bool KeyWinKey = false;
int  SnapKeys  = 0;     // 2 = Left, 4 = Up, 8 = Right, 16 = Down

// Holds swapchain wait handles
std::vector<HANDLE> vSwapchainWaitHandles;

// GOG Galaxy stuff
std::wstring GOGGalaxy_Path        = L"";
std::wstring GOGGalaxy_Folder      = L"";
std::wstring GOGGalaxy_UserID      = L"";
bool         GOGGalaxy_Installed   = false;

DWORD    RepopulateGamesWasSet     = 0;
bool     RepopulateGames           = false,
         RefreshSettingsTab        = false;
uint32_t SelectNewSKIFGame         = 0;

bool  HoverTipActive               = false;
DWORD HoverTipDuration             = 0;

// Notification icon stuff
static const GUID SKIF_NOTIFY_GUID = // {8142287D-5BC6-4131-95CD-709A2613E1F5}
{ 0x8142287d, 0x5bc6, 0x4131, { 0x95, 0xcd, 0x70, 0x9a, 0x26, 0x13, 0xe1, 0xf5 } };
#define SKIF_NOTIFY_ICON                    0x1330 // 4912
#define SKIF_NOTIFY_EXIT                    0x1331 // 4913
#define SKIF_NOTIFY_START                   0x1332 // 4914
#define SKIF_NOTIFY_STOP                    0x1333 // 4915
#define SKIF_NOTIFY_STARTWITHSTOP           0x1334 // 4916
#define SKIF_NOTIFY_RUN_UPDATER             0x1335 // 4917
#define WM_SKIF_NOTIFY_ICON      (WM_USER + 0x150) // 1360
NOTIFYICONDATA niData;
HMENU hMenu;

// Cmd line argument stuff
SKIF_Signals _Signal;

PopupState UpdatePromptPopup = PopupState_Closed;
PopupState HistoryPopup      = PopupState_Closed;
PopupState AutoUpdatePopup   = PopupState_Closed;
UITab SKIF_Tab_Selected      = UITab_Viewer,
      SKIF_Tab_ChangeTo      = UITab_None;
extern PopupState  OpenFileDialog;  // Viewer: open file dialog
extern PopupState  SaveFileDialog;  // Viewer: save file dialog
extern PopupState  ExportSDRDialog; // Viewer: export sdr dialog

HMODULE hModSKIF     = nullptr;
HMODULE hModSpecialK = nullptr;
HICON   hIcon        = nullptr;
#define GCL_HICON      (-14)

// Texture related locks to prevent driver crashes
concurrency::concurrent_queue <IUnknown*> SKIF_ResourcesToFree; // CComPtr <IUnknown>

float fBottomDist = 0.0f;

ID3D11Device*           SKIF_pd3dDevice           = nullptr;
ID3D11DeviceContext*    SKIF_pd3dDeviceContext    = nullptr;
//ID3D11RenderTargetView* SKIF_g_mainRenderTargetView = nullptr;

// Forward declarations
bool                CreateDeviceD3D                           (HWND hWnd);
void                CleanupDeviceD3D                          (void);
LRESULT WINAPI      SKIF_WndProc                              (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI      SKIF_Notify_WndProc                       (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void                SKIF_Initialize                           (LPWSTR lpCmdLine);

// Holds current global DPI scaling, 1.0f == 100%, 1.5f == 150%.
float SKIF_ImGui_GlobalDPIScale      = 1.0f;
// Holds last frame's DPI scaling
float SKIF_ImGui_GlobalDPIScale_Last = 1.0f;
//float SKIF_ImGui_GlobalDPIScale_New  = 1.0f;
float SKIF_ImGui_FontSizeDefault     = 18.0f; // 18.0F

std::string SKIF_StatusBarText = "";
std::string SKIF_StatusBarHelp = "";
HWND        SKIF_ImGui_hWnd    = NULL;
HWND        SKIF_Notify_hWnd   = NULL;

HWND  hWndOrigForeground;
HWND  hWndForegroundFocusOnExit = nullptr; // Game HWND as reported by Special K through WM_SKIF_EVENT_SIGNAL
DWORD pidForegroundFocusOnExit  = NULL;    // Used to hold the game process ID that SKIF launched

void CALLBACK
SKIF_EfficiencyModeTimerProc (HWND hWnd, UINT Msg, UINT wParamIDEvent, DWORD dwTime)
{
  UNREFERENCED_PARAMETER (Msg);
  UNREFERENCED_PARAMETER (wParamIDEvent);
  UNREFERENCED_PARAMETER (dwTime);

  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  KillTimer (hWnd, cIDT_TIMER_EFFICIENCY);

  extern bool tryingToLoadImage;

  if (_registry.bEfficiencyMode && ! _registry._EfficiencyMode && ! SKIF_ImGui_IsFocused ( ) && ! tryingToLoadImage)
  {
    _registry._EfficiencyMode = true;
    msgDontRedraw = true;

    PLOG_DEBUG << "Engaging efficiency mode";

    // Enable Efficiency Mode in Windows 11 (requires idle (low) priority + EcoQoS)
    SKIF_Util_SetProcessPowerThrottling (SKIF_Util_GetCurrentProcess(), 1);
    SetPriorityClass (SKIF_Util_GetCurrentProcess(), IDLE_PRIORITY_CLASS );
  }
}

void
SKIF_Startup_ProcessCmdLineArgs (LPWSTR lpCmdLine)
{
  // Use specific shorthands for our internal tasks instead of
  //   a case insensitive search which would produce false positives
  _Signal.Quit            = 
    _wcsicmp (lpCmdLine, L"/Exit") == NULL;
  _Signal.Minimize        = 
    wcscmp (lpCmdLine, L"1") == NULL;
//_Signal.CheckForUpdates = 
//  wcscmp (lpCmdLine, L"2") == NULL;
  _Signal.OpenFileDialog = 
    _wcsicmp (lpCmdLine, L"/OpenFileDialog") == NULL;

  if (! _Signal.Quit     &&
      ! _Signal.Minimize &&
      ! _Signal.OpenFileDialog)
    _Signal._FilePath = std::wstring(lpCmdLine);

  SKIF_Util_TrimLeadingSpacesW (_Signal._FilePath);

  _Signal._RunningInstance =
    FindWindowExW (0, 0, SKIF_NotifyIcoClass, nullptr);
}

void
SKIF_Startup_CopyDataRunningInstance (void)
{
  if (! _Signal._RunningInstance)
    return;

  if (_Signal._FilePath.empty())
    return;

  wchar_t                    wszFilePath [MAX_PATH] = { };
  if (S_OK == StringCbCopyW (wszFilePath, MAX_PATH, _Signal._FilePath.data()))
  {
    COPYDATASTRUCT cds { };
    cds.dwData = SKIV_CDS_STRING;
    cds.lpData = &wszFilePath;
    cds.cbData = sizeof(wszFilePath);

    // We must allow the running instance to set foreground window
    AllowSetForegroundWindow (SKIF_Util_GetProcessIdFromHwnd (_Signal._RunningInstance));

    // If the running instance returns true on our WM_COPYDATA call,
    //   that means this instance has done its job and can be closed.
    if (SendMessage (_Signal._RunningInstance,
                  WM_COPYDATA,
                  NULL, //(WPARAM)(HWND) NULL
                  (LPARAM) (LPVOID) &cds))
      ExitProcess (0x0);
  }
}

void
SKIF_Startup_CloseRunningInstances (void)
{
  if (! _Signal._RunningInstance)
    return;

  if (_Signal._FilePath.empty())
    return;

  PLOG_INFO << "Another instance is already running, however this instance takes priority.";
  PLOG_INFO << "Closing the remaining ones...";

  struct custom_s
  {
    const wchar_t*      windowClass;
    std::vector<HANDLE> handles;
  } shared;

  shared.windowClass = SKIF_NotifyIcoClass;

  // Send WM_CLOSE to other running instances (not ourselves, as we don't have a window yet)
  EnumWindows ( []( HWND   hWnd,
                    LPARAM lParam ) -> BOOL
  {
    auto pShared = reinterpret_cast<custom_s*>(lParam);
    wchar_t                         wszRealWindowClass [64] = { };
    if (RealGetWindowClassW (hWnd,  wszRealWindowClass, 64))
      if (StrCmpIW (pShared->windowClass, wszRealWindowClass) == 0)
      {
        HANDLE handle = SKIF_Util_GetProcessHandleFromHwnd (hWnd, PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE);
        if (handle != NULL)
          pShared->handles.push_back (handle);
        PostMessage (hWnd, WM_CLOSE, 0x0, 0x0);
      }
    return TRUE;
  }, reinterpret_cast<LPARAM>(&shared));

  // Wait on the handles we retrieved
  if (! shared.handles.empty())
  {
    DWORD res =
      WaitForMultipleObjectsEx (static_cast<DWORD>(shared.handles.size()), shared.handles.data(), true, 2500, false);
    
    if (res == WAIT_FAILED)
      PLOG_DEBUG << "Failed when trying to wait on the running instances!";
    else if (res == WAIT_TIMEOUT)
      PLOG_DEBUG << "Timed out when waiting on the running instances!";
    else if (WAIT_OBJECT_0 <= res && res <= (WAIT_OBJECT_0 + static_cast<DWORD>(shared.handles.size()) - 1))
      PLOG_DEBUG << "Successfully waited on all running instances.";
  }
}

void
SKIF_Startup_ProxyCommandLineArguments (void)
{
  if (! _Signal._RunningInstance)
    return;

  if (! _Signal.Minimize         &&
      ! _Signal.OpenFileDialog   &&
      ! _Signal.CheckForUpdates  &&
      ! _Signal.Quit)
    return;

  PLOG_INFO << "Proxying command line arguments...";

  if (_Signal.Minimize)
  {
    //PostMessage (_Signal._RunningInstance, WM_SKIF_MINIMIZE, 0x0, 0x0);

    // Send WM_SKIF_MINIMIZE to all running instances (including ourselves)
    EnumWindows ( []( HWND   hWnd,
                      LPARAM lParam ) -> BOOL
    {
      wchar_t                         wszRealWindowClass [64] = { };
      if (RealGetWindowClassW (hWnd,  wszRealWindowClass, 64))
        if (StrCmpIW ((LPWSTR)lParam, wszRealWindowClass) == 0)
          PostMessage (hWnd, WM_SKIF_MINIMIZE, 0x0, 0x0);
      return TRUE;
    }, (LPARAM)SKIF_NotifyIcoClass);
  }

  if (_Signal.OpenFileDialog)
  {
    // Send WM_SKIF_FILE_DIALOG to a single running instance (including ourselves)
    EnumWindows ( []( HWND   hWnd,
                      LPARAM lParam ) -> BOOL
    {
      wchar_t                         wszRealWindowClass [64] = { };
      if (RealGetWindowClassW (hWnd,  wszRealWindowClass, 64))
        if (StrCmpIW ((LPWSTR)lParam, wszRealWindowClass) == 0)
        {
          PostMessage (hWnd, WM_SKIF_FILE_DIALOG, 0x0, 0x0);
          return FALSE;
        }
      return TRUE;
    }, (LPARAM)SKIF_NotifyIcoClass);
  }

  if (_Signal.CheckForUpdates)
  {
    // PostMessage (_Signal._RunningInstance, WM_SKIF_RUN_UPDATER, 0x0, 0x0);

    // Send WM_SKIF_RUN_UPDATER to all running instances (including ourselves)
    EnumWindows ( []( HWND   hWnd,
                      LPARAM lParam ) -> BOOL
    {
      wchar_t                         wszRealWindowClass [64] = { };
      if (RealGetWindowClassW (hWnd,  wszRealWindowClass, 64))
        if (StrCmpIW ((LPWSTR)lParam, wszRealWindowClass) == 0)
          PostMessage (hWnd, WM_SKIF_RUN_UPDATER, 0x0, 0x0);
      return TRUE;
    }, (LPARAM)SKIF_NotifyIcoClass);
  }

  if (_Signal.Quit)
  {
    // PostMessage (_Signal._RunningInstance, WM_CLOSE, 0x0, 0x0);

    // Send WM_CLOSE to all running instances (including ourselves)
    EnumWindows ( []( HWND   hWnd,
                      LPARAM lParam ) -> BOOL
    {
      wchar_t                         wszRealWindowClass [64] = { };
      if (RealGetWindowClassW (hWnd,  wszRealWindowClass, 64))
        if (StrCmpIW ((LPWSTR)lParam, wszRealWindowClass) == 0)
          PostMessage (hWnd, WM_CLOSE, 0x0, 0x0);
      return TRUE;
    }, (LPARAM)SKIF_NotifyIcoClass);
  }

  // Restore the foreground state to whatever app had it before
  if (SKIF_Notify_hWnd == NULL)
  {
    if (hWndOrigForeground != 0)
    {
      if (IsIconic        (hWndOrigForeground))
        ShowWindow        (hWndOrigForeground, SW_SHOWNA);
      SetForegroundWindow (hWndOrigForeground);
    }

    PLOG_INFO << "Terminating due to this instance having done its job.";
    ExitProcess (0x0);
  }
}

void
SKIF_Startup_RaiseRunningInstance (void)
{
  if (! _Signal._RunningInstance)
    return;

  if (! _Signal._FilePath.empty())
    return;

  // We must allow the existing process to set the foreground window
  //   as this is part of the WM_SKIF_RESTORE procedure
  DWORD pidAlreadyExists = 0;
  GetWindowThreadProcessId (_Signal._RunningInstance, &pidAlreadyExists);
  if (pidAlreadyExists)
    AllowSetForegroundWindow (pidAlreadyExists);

  PLOG_INFO << "Attempting to restore the running instance: " << pidAlreadyExists;
  SendMessage (_Signal._RunningInstance, WM_SKIF_RESTORE, 0x0, 0x0);
  
  PLOG_INFO << "Terminating due to this instance having done its job.";
  ExitProcess (0x0);
}

void SKIF_Shell_CreateUpdateNotifyMenu (void)
{
  if (hMenu != NULL)
    DestroyMenu (hMenu);

  hMenu = CreatePopupMenu ( );
  if (hMenu != NULL)
  {
  //AppendMenu (hMenu, MF_STRING, SKIF_NOTIFY_RUN_UPDATER,   L"Open file...");
  //AppendMenu (hMenu, MF_STRING | ((svcStopped)         ? MF_CHECKED | MF_GRAYED :                                    0x0), SKIF_NOTIFY_STOP,          L"Stop Service");
  //AppendMenu (hMenu, MF_SEPARATOR, 0, NULL);
  //AppendMenu (hMenu, MF_STRING, SKIF_NOTIFY_RUN_UPDATER,   L"Check for updates...");
  //AppendMenu (hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu (hMenu, MF_STRING, SKIF_NOTIFY_EXIT,          L"Exit");
  }
}

// This creates a notification icon
void SKIF_Shell_CreateNotifyIcon (void)
{
  ZeroMemory (&niData,  sizeof (NOTIFYICONDATA));
  niData.cbSize       = sizeof (NOTIFYICONDATA); // 6.0.6 or higher (Windows Vista and later)
  niData.uID          = SKIF_NOTIFY_ICON;
//niData.guidItem     = SKIF_NOTIFY_GUID; // Prevents notification icons from appearing for separate running instances
  niData.uFlags       = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP; // NIF_GUID
  niData.hIcon        = LoadIcon (hModSKIF, MAKEINTRESOURCE (IDI_SKIV));
  niData.hWnd         = SKIF_Notify_hWnd;
  niData.uVersion     = NOTIFYICON_VERSION_4;
  wcsncpy_s (niData.szTip,      128, L"Special K",   128);

  niData.uCallbackMessage = WM_SKIF_NOTIFY_ICON;

  Shell_NotifyIcon (NIM_ADD, &niData);
  //Shell_NotifyIcon (NIM_SETVERSION, &niData); // Breaks shit, lol
}

// This populates the notification icon with the appropriate icon
void SKIF_Shell_UpdateNotifyIcon (void)
{
  niData.uFlags       = NIF_ICON;
  niData.hIcon        = LoadIcon (hModSKIF, MAKEINTRESOURCE (IDI_SKIV));
  Shell_NotifyIcon (NIM_MODIFY, &niData);
}

// This deletes the notification icon
void SKIF_Shell_DeleteNotifyIcon (void)
{
  Shell_NotifyIcon (NIM_DELETE, &niData);
  DeleteObject     (niData.hIcon);
  niData.hIcon = 0;
}

// Show a desktop notification
// SKIF_NTOAST_UPDATE  - Appears always
// SKIF_NTOAST_SERVICE - Appears conditionally
void SKIF_Shell_CreateNotifyToast (UINT type, std::wstring message, std::wstring title = L"")
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  if (type == SKIF_NTOAST_UPDATE || _registry.bNotifications)
  {
    niData.uFlags       = 
        NIF_INFO  | NIF_REALTIME;  // NIF_REALTIME to indicate the notifications should be discarded if not displayed immediately

    niData.dwInfoFlags  = 
      (type == SKIF_NTOAST_SERVICE)
      ? NIIF_NONE | NIIF_RESPECT_QUIET_TIME | NIIF_NOSOUND // Mute the sound for service notifications
      : NIIF_NONE | NIIF_RESPECT_QUIET_TIME;
    wcsncpy_s (niData.szInfoTitle, 64,   title.c_str(),  64);
    wcsncpy_s (niData.szInfo,     256, message.c_str(), 256);

    Shell_NotifyIcon (NIM_MODIFY, &niData);

    // Set up a timer that automatically refreshes SKIF when the notification clears,
    //   allowing us to perform some maintenance and whatnot when that occurs
    SetTimer (SKIF_Notify_hWnd, IDT_REFRESH_NOTIFY, _registry.iNotificationsDuration, NULL);
  }
}

void SKIF_Shell_CreateJumpList (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  CComPtr <ICustomDestinationList>   pDestList;                                 // The jump list
  CComPtr <IObjectCollection>        pObjColl;                                  // Object collection to hold the custom tasks.
  CComPtr <IShellLink>               pLink;                                     // Reused for the custom tasks
  CComPtr <IObjectArray>             pRemovedItems;                             // Not actually used since we don't carry custom destinations
  PROPVARIANT                        pv;                                        // Used to give the custom tasks a title
  UINT                               cMaxSlots;                                 // Not actually used since we don't carry custom destinations
       
  // Create a jump list COM object.
  if     (SUCCEEDED (pDestList.CoCreateInstance (CLSID_DestinationList)))
  {
    pDestList     ->SetAppID        (SKIF_AppUserModelID);
    pDestList     ->BeginList       (&cMaxSlots, IID_PPV_ARGS (&pRemovedItems));

    pDestList     ->AppendKnownCategory (KDC_RECENT);

    if   (SUCCEEDED (pObjColl.CoCreateInstance (CLSID_EnumerableObjectCollection)))
    {
      // Task #1: /OpenFileDialog
      if (SUCCEEDED (pLink.CoCreateInstance (CLSID_ShellLink)))
      {
        CComQIPtr <IPropertyStore>   pPropStore = pLink.p;                      // The link title is kept in the object's property store, so QI for that interface.

        pLink     ->SetPath         (_path_cache.skiv_executable);
        pLink     ->SetArguments    (L"/OpenFileDialog");                       // Set the arguments
        pLink     ->SetIconLocation (_path_cache.skiv_executable, 0);           // Set the icon location.
        pLink     ->SetDescription  (L"Open the file dialog");                  // Set the link description (tooltip on the jump list item)
        InitPropVariantFromString   (L"Open", &pv);
        pPropStore->SetValue                   (PKEY_Title, pv);                // Set the title property.
        PropVariantClear                                  (&pv);
        pPropStore->Commit          ( );                                        // Save the changes we made to the property store
        pObjColl  ->AddObject       (pLink);                                    // Add this shell link to the object collection.
        pPropStore .Release         ( );
        pLink      .Release         ( );
      }

      /*
      // Separator
      if (SUCCEEDED (pLink.CoCreateInstance (CLSID_ShellLink)))
      {
        CComQIPtr <IPropertyStore>   pPropStore = pLink.p;                      // The link title is kept in the object's property store, so QI for that interface.

        InitPropVariantFromBoolean  (TRUE, &pv);
        pPropStore->SetValue (PKEY_AppUserModel_IsDestListSeparator, pv);       // Set the separator property.
        PropVariantClear                                  (&pv);
        pPropStore->Commit          ( );                                        // Save the changes we made to the property store
        pObjColl  ->AddObject       (pLink);                                    // Add this shell link to the object collection.
        pPropStore .Release         ( );
        pLink      .Release         ( );
      }
      */

      // Task #5: Exit -- Not actually needed since Windows exposes a "Close window" and "Close all windows" option in the jump list
      /*
      if (SUCCEEDED (pLink.CoCreateInstance (CLSID_ShellLink)))
      {
        CComQIPtr <IPropertyStore>   pPropStore = pLink.p;                      // The link title is kept in the object's property store, so QI for that interface.

        pLink     ->SetPath         (_path_cache.skiv_executable);
        pLink     ->SetArguments    (L"/Exit");                                 // Set the arguments
        pLink     ->SetIconLocation (_path_cache.skiv_executable, 0);           // Set the icon location.
      //pLink     ->SetDescription  (L"Closes the application");                // Set the link description (tooltip on the jump list item)
        InitPropVariantFromString   (L"Exit", &pv);
        pPropStore->SetValue                (PKEY_Title, pv);                   // Set the title property.
        PropVariantClear                               (&pv);
        pPropStore->Commit          ( );                                        // Save the changes we made to the property store
        pObjColl  ->AddObject       (pLink);                                    // Add this shell link to the object collection.
        pPropStore .Release         ( );
        pLink      .Release         ( );
      }
      */

      CComQIPtr <IObjectArray>       pTasksArray = pObjColl.p;                  // Get an IObjectArray interface for AddUserTasks.
      pDestList   ->AddUserTasks    (pTasksArray);                              // Add the tasks to the jump list.
      pDestList   ->CommitList      ( );                                        // Save the jump list.
      pTasksArray  .Release         ( );

      pObjColl     .Release         ( );
    }
    else
      PLOG_ERROR << "Failed to create CLSID_EnumerableObjectCollection object!";

    pDestList      .Release         ( );
  }
  else
    PLOG_ERROR << "Failed to create CLSID_DestinationList object!";
}

void SKIF_Shell_AddJumpList (std::wstring name, std::wstring path, std::wstring parameters, std::wstring directory, std::wstring icon_path, bool bService)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  CComPtr <IShellLink>               pLink;                                     // Reused for the custom tasks
  PROPVARIANT                        pv;                                        // Used to give the custom tasks a title

  if (SUCCEEDED (pLink.CoCreateInstance (CLSID_ShellLink)))
  {
    CComQIPtr <IPropertyStore>   pPropStore = pLink.p;                      // The link title is kept in the object's property store, so QI for that interface.

    bool uriLaunch = (! PathFileExists (path.c_str()));

    if (uriLaunch)
    {
      parameters = L"SKIF_URI=" + parameters;

      if (! bService)
        name       = name + L" (w/o Special K)";
      else
        parameters = L"Start Temp " + parameters;
    }

    else {
      parameters = SK_FormatStringW (LR"("%ws" %ws)", path.c_str(), parameters.c_str());
    }

    pLink     ->SetPath             (_path_cache.skiv_executable);         // Point to SKIF.exe
    pLink     ->SetArguments        (parameters.c_str());                  // Set the arguments

    if (! directory.empty())
      pLink   ->SetWorkingDirectory (directory.c_str());                   // Set the working directory

    if (PathFileExists (icon_path.c_str()))
      pLink   ->SetIconLocation     (icon_path.c_str(), 0);                // Set the icon location

    pLink     ->SetDescription      (parameters.c_str());                  // Set the link description (tooltip on the jump list item)

    InitPropVariantFromString       (name.c_str(), &pv);
    pPropStore->SetValue            (PKEY_Title,    pv);                   // Set the title property
    PropVariantClear                              (&pv);

    pPropStore->Commit              ( );                                   // Save the changes we made to the property store

    SHAddToRecentDocs               (SHARD_LINK, pLink.p);                 // Add to the Recent list

    pPropStore .Release             ( );
    pLink      .Release             ( );
  }
}


void SKIF_Initialize (LPWSTR lpCmdLine)
{
  static bool isInitalized = false;

  if (isInitalized)
    return;

  isInitalized = true;

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  // Let's change the current working directory to the folder of the executable itself.
  SetCurrentDirectory (_path_cache.skiv_install);

  // Generate 8.3 filenames
  //SK_Generate8Dot3    (_path_cache.skif_workdir_org);
  SK_Generate8Dot3    (_path_cache.skiv_install);

  bool fallback = true;

  // See if we can interact with the install folder
  // This section of the code triggers a refresh of the DLL files for other running SKIF instances
  // TODO: Find a better way to determine permissions that does not rely on creating dummy files/folders?
  std::filesystem::path testDir  (_path_cache.skiv_install);
  std::filesystem::path testFile (testDir);

  testDir  /= L"SKIVTMPDIR";
  testFile /= L"SKIVTMPFILE.tmp";

  // Try to delete any existing tmp folder or file (won't throw an exception at least)
  RemoveDirectory (testDir.c_str());
  DeleteFile      (testFile.wstring().c_str());

  std::error_code ec;
  // See if we can create a folder
  if (! std::filesystem::exists (            testDir, ec) &&
        std::filesystem::create_directories (testDir, ec))
  {
    // Delete it
    RemoveDirectory (testDir.c_str());

    // See if we can create a file
    HANDLE h = CreateFile ( testFile.wstring().c_str(),
            GENERIC_READ | GENERIC_WRITE,
              FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                  CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL,
                      NULL );

    // If the file was created successfully
    if (h != INVALID_HANDLE_VALUE)
    {
      // We need to close the handle as well
      CloseHandle (h);

      // Delete it
      DeleteFile (testFile.wstring().c_str());

      // Use current path as we have write permissions
      wcsncpy_s ( _path_cache.skiv_userdata, MAX_PATH,
                  _path_cache.skiv_install, _TRUNCATE );

      // No need to rely on the fallback
      fallback = false;
    }
  }

  if (fallback)
  {
    // Fall back to appdata in case of issues
    std::wstring fallbackDir =
      std::wstring (_path_cache.app_data_local.path) + LR"(\Special K\Viewer\)";

    wcsncpy_s ( _path_cache.skiv_userdata, MAX_PATH,
                fallbackDir.c_str(), _TRUNCATE);
        
    // Create any missing directories
    if (! std::filesystem::exists (            fallbackDir, ec))
          std::filesystem::create_directories (fallbackDir, ec);
  }

  // Now we can proceed with initializing the logging
  std::wstring logPath =
    SK_FormatStringW (LR"(%ws\SKIV.log)", _path_cache.skiv_userdata);

  std::wstring logPath_old = logPath + L".bak";

  // Delete the .old log file and rename any previous log to .old
  DeleteFile (logPath_old.c_str());
  MoveFile   (logPath.c_str(), logPath_old.c_str());

  // Engage logging!
  static plog::RollingFileAppender<plog::LogFormatterUtcTime> fileAppender(logPath.c_str(), 10000000, 1);
  plog::init (plog::debug, &fileAppender);

  // Let us do a one-time check if a debugger is attached,
  //   and if so set up PLOG to push logs there as well
  BOOL isRemoteDebuggerPresent = FALSE;
  CheckRemoteDebuggerPresent (SKIF_Util_GetCurrentProcess(), &isRemoteDebuggerPresent);

  if (isRemoteDebuggerPresent || IsDebuggerPresent())
  {
    static plog::DebugOutputAppender<plog::TxtFormatter> debugOutputAppender;
    plog::get()->addAppender (&debugOutputAppender);

    SKIF_debuggerPresent = true;
  }
  
  GetCurrentDirectoryW    (MAX_PATH, _path_cache.skiv_workdir);


#ifdef _WIN64
  PLOG_INFO << "Initializing Special K Image Viewer (SKIV) 64-bit..."
#else
  PLOG_INFO << "Initializing Special K Image Viewer (SKIV) 32-bit..."
#endif
            << "\n+------------------+-------------------------------------+"
            << "\n| Executable       | " << _path_cache.skiv_executable
            << "\n|    > version     | " << SKIV_VERSION_STR_A
            << "\n|    > build       | " << __DATE__ ", " __TIME__
            << "\n|    > mode        | " << "Regular"
            << "\n|    > arguments   | " << lpCmdLine
            << "\n|    > directory   | "
            << "\n|      > original  | " << _path_cache.skiv_workdir_org
            << "\n|      > adjusted  | " << _path_cache.skiv_workdir
            << "\n| Location         | " << _path_cache.skiv_install
            << "\n|    > user data   | " << _path_cache.skiv_userdata
            << "\n+------------------+-------------------------------------+";

  // SKIV also uses a folder to temporary internet files
  const std::wstring tempDir =
    std::wstring (_path_cache.app_data_local.path) + LR"(\Temp\skiv\)";

  wcsncpy_s (_path_cache.skiv_temp, MAX_PATH,
                  tempDir.c_str(), _TRUNCATE);

  // Clear out any temp files older than a day
  if (PathFileExists (_path_cache.skiv_temp))
  {
    auto _isDayOld = [&](FILETIME ftLastWriteTime) -> bool
    {
      FILETIME ftSystemTime{}, ftAdjustedFileTime{};
      SYSTEMTIME systemTime{};
      GetSystemTime (&systemTime);

      if (SystemTimeToFileTime(&systemTime, &ftSystemTime))
      {
        ULARGE_INTEGER uintLastWriteTime{};

        // Copy to ULARGE_INTEGER union to perform 64-bit arithmetic
        uintLastWriteTime.HighPart        = ftLastWriteTime.dwHighDateTime;
        uintLastWriteTime.LowPart         = ftLastWriteTime.dwLowDateTime;

        // Perform 64-bit arithmetic to add 1 day to last modified timestamp
        uintLastWriteTime.QuadPart        = uintLastWriteTime.QuadPart + ULONGLONG(1 * 24 * 60 * 60 * 1.0e+7);

        // Copy the results to an FILETIME struct
        ftAdjustedFileTime.dwHighDateTime = uintLastWriteTime.HighPart;
        ftAdjustedFileTime.dwLowDateTime  = uintLastWriteTime.LowPart;

        // Compare with system time, and if system time is later (1), then return true
        if (CompareFileTime (&ftSystemTime, &ftAdjustedFileTime) == 1)
          return true;
      }

      return false;
    };

    HANDLE hFind        = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATA ffd = { };

    hFind = 
      FindFirstFileExW ((tempDir + L"*").c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, NULL);

    if (INVALID_HANDLE_VALUE != hFind)
    {
      if (_isDayOld  (ffd.ftLastWriteTime))
        DeleteFile  ((tempDir + ffd.cFileName).c_str());

      while (FindNextFile (hFind, &ffd))
        if (_isDayOld  (ffd.ftLastWriteTime))
          DeleteFile  ((tempDir + ffd.cFileName).c_str());

      FindClose (hFind);
    }
  }
}

bool bKeepWindowAlive  = true,
     bKeepProcessAlive = true;

// Uninstall registry keys
// Current User: HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall\{F4A43527-9457-424A-90A6-17CF02ACF677}_is1
// All Users:   HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{F4A43527-9457-424A-90A6-17CF02ACF677}_is1

// Install folders
// Legacy:                              Documents\My Mods\SpecialK
// Modern (Current User; non-elevated): %LOCALAPPDATA%\Programs\Special K
// Modern (All Users;        elevated): %PROGRAMFILES%\Special K

// Main code
int
APIENTRY
wWinMain ( _In_     HINSTANCE hInstance,
           _In_opt_ HINSTANCE hPrevInstance,
           _In_     LPWSTR    lpCmdLine,
           _In_     int       nCmdShow )
{
  UNREFERENCED_PARAMETER (hPrevInstance);
  UNREFERENCED_PARAMETER (hInstance);

  SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOALIGNMENTFAULTEXCEPT);
  
  SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIV_MainThread");

  //CoInitializeEx (nullptr, 0x0);
  OleInitialize (NULL); // Needed for IDropTarget

  // All DbgHelp functions, such as ImageNtHeader, are single threaded.
  extern   CRITICAL_SECTION   CriticalSectionDbgHelp;
  InitializeCriticalSection (&CriticalSectionDbgHelp);

  // Get the current time to use as a basis of how long the initialization took
  SKIF_startupTime = SKIF_Util_timeGetTime1();
  
  // Process cmd line arguments (1/4) -- this sets up the necessary variables
  SKIF_Startup_ProcessCmdLineArgs (lpCmdLine);

  // This constructs these singleton objects
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( ); // Does not rely on anything
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( ); // Does not rely on anything
  
  // Process cmd line arguments (2/4)
  hWndOrigForeground = // Remember what third-party window is currently in the foreground
    GetForegroundWindow ( );

  // If an instance is already running and we disallow
  //   multiple running then we need to handle this by
  //     closing either that one or this one...
  //
  // Process cmd line arguments (3/4)
  if (_Signal._RunningInstance)
  {
    // NOTE: Logging hasn't been initialized at this point!

    // If we have a file incoming, and we disallow multiple instances
    //   then we need to close any other running ones so we survive
    if (! _registry.bMultipleInstances)
    {
      // Try to send data over using WM_COPYDATA,
      //   and terminate the instance if it succeeds
      SKIF_Startup_CopyDataRunningInstance   ( );
      
      // The below calls only occurs if WM_COPYDATA failed
      SKIF_Startup_CloseRunningInstances   ( );
    }

    // The below only execute when there are commands to proxy
    SKIF_Startup_ProxyCommandLineArguments ( );

    if (! _registry.bMultipleInstances)
      SKIF_Startup_RaiseRunningInstance    ( );
  }

  // Initialize SKIF
  SKIF_Initialize (lpCmdLine); // Relies on _path_cache and sets up logging

  plog::get()->setMaxSeverity((plog::Severity) _registry.iLogging);

  PLOG_INFO << "Max severity to log was set to " << _registry.iLogging;

  // Update the path cache's SK user data folder based on what we read from the registry
  wcsncpy_s ( _path_cache.specialk_userdata, MAX_PATH,
              _registry.wsPathSpecialK.c_str(), _TRUNCATE);

  // Set process preference to E-cores using only CPU sets, :)
  //  as affinity masks are inherited by child processes... :(
  SKIF_Util_SetProcessPrefersECores ( );

  // Sets the current process app user model ID (used for jump lists and the like)
  if (FAILED (SetCurrentProcessExplicitAppUserModelID (SKIF_AppUserModelID)))
    PLOG_ERROR << "Call to SetCurrentProcessExplicitAppUserModelID failed!";

  // Load the SKIF.exe module (used to populate the icon here and there)
  hModSKIF =
    GetModuleHandleW (nullptr);

  //SKIF_Util_Debug_LogUserNames ( );

  if (_Signal.Minimize)
    nCmdShow = SW_SHOWMINNOACTIVE;

  if (nCmdShow == SW_SHOWMINNOACTIVE)
    startedMinimized = true;
  else if (nCmdShow == SW_HIDE)
    startedMinimized = true;

  SKIF_nCmdShow = nCmdShow;

  // Register SKIV in Windows to enable quick launching.
  if (! _Signal.Quit)
  {
    PLOG_INFO << "Checking global registry values..."
              << "\n+------------------+-------------------------------------+"
              << "\n| Central path     | " << _registry.wsPathViewer
              << "\n| App registration | " << _registry.wsAppRegistration
              << "\n+------------------+-------------------------------------+";

    // Always force registration for SKIV
    SKIF_Util_RegisterApp (true);
  }

  PLOG_INFO << "Creating notification icon...";

  // Create invisible notify window (for the traybar icon and notification toasts, and for doing D3D11 tests)
  WNDCLASSEX wcNotify =
  { sizeof (WNDCLASSEX),
            CS_CLASSDC, SKIF_Notify_WndProc,
            0L,         0L,
        NULL, nullptr,  nullptr,
              nullptr,  nullptr,
              SKIF_NotifyIcoClass,
              nullptr          };

  if (! ::RegisterClassEx (&wcNotify))
  {
    return 0;
  }

  SKIF_Notify_hWnd      =
    CreateWindowExW (                                            WS_EX_NOACTIVATE,
      wcNotify.lpszClassName, _T("Special K Notification Icon"), WS_ICONIC,
                         0, 0,
                         0, 0,
                   nullptr, nullptr,
        wcNotify.hInstance, nullptr
    );

  hIcon = LoadIcon (hModSKIF, MAKEINTRESOURCE (IDI_SKIV));

#if 0
  // The notify window has been created but not displayed.
  // Now we have a parent window to which a notification tray icon can be associated.
  SKIF_Shell_CreateNotifyIcon       ();
  SKIF_Shell_CreateUpdateNotifyMenu ();
#endif

  // Initialize the gamepad input child thread
  static SKIF_GamePadInputHelper& _gamepad =
         SKIF_GamePadInputHelper::GetInstance ( );

  // Register for device notifications
  _gamepad.RegisterDevNotification  (SKIF_Notify_hWnd);

  // Process cmd line arguments (4/4)
  if (! _Signal._FilePath.empty())
  {
    extern std::wstring dragDroppedFilePath;
    dragDroppedFilePath = _Signal._FilePath;
  }

  else {
    _Signal._RunningInstance = SKIF_Notify_hWnd;
    SKIF_Startup_ProxyCommandLineArguments ( );
  }
  
  PLOG_INFO << "Initializing Direct3D...";

  DWORD temp_time = SKIF_Util_timeGetTime1();

  // Initialize Direct3D
  if (! CreateDeviceD3D (SKIF_Notify_hWnd))
  {
    CleanupDeviceD3D ();
    return 1;
  }

  PLOG_DEBUG << "Operation [CreateDeviceD3D] took " << (SKIF_Util_timeGetTime1() - temp_time) << " ms.";

  // Register to be notified if the effective power mode changes
  //SKIF_Util_SetEffectivePowerModeNotifications (true); // (this serves no purpose yet)

  // The DropTarget object used for drag-and-drop support for new covers
  static SKIF_DropTargetObject& _drag_drop  = SKIF_DropTargetObject::GetInstance ( );
  extern std::wstring dragDroppedFilePath;
  
  PLOG_INFO << "Initializing ImGui...";

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION   ();
  ImGui::CreateContext ();

  ImGuiIO& io =
    ImGui::GetIO ();

  (void)io; // WTF does this do?!

  io.IniFilename = "SKIV.ini";                                // nullptr to disable imgui.ini
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;        // Enable Gamepad Controls
//io.ConfigFlags |= ImGuiConfigFlags_NavEnableSetMousePos;
//io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;     // FIXME-DPI: THIS CURRENTLY DOESN'T WORK AS EXPECTED. DON'T USE IN USER APP! 
//io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports; // FIXME-DPI

  // Viewports
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
  io.ConfigViewportsNoAutoMerge      = false;
  io.ConfigViewportsNoTaskBarIcon    = false;
  io.ConfigViewportsNoDecoration     = false; // We want decoration (OS-provided animations etc)
  io.ConfigViewportsNoDefaultParent  = false;

  // Docking
//io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
  io.ConfigDockingAlwaysTabBar       = false;
  io.ConfigDockingTransparentPayload =  true;

  // Main window override flags
  ImGuiWindowClass SKIF_AppWindow;
  // This prevents the main window from ever being merged into the implicit Debug##Default fallback window...
  // ... which works around a pesky bug that occurs on OS snapping/resizing...
  // ... and prevents the window from constantly being re-created infinitely...
  SKIF_AppWindow.ViewportFlagsOverrideSet |= ImGuiViewportFlags_NoAutoMerge;
//SKIF_AppWindow.ViewportFlagsOverrideSet |= ImGuiViewportFlags_CanHostOtherWindows;

  // Enable ImGui's debug logging output
  ImGui::GetCurrentContext()->DebugLogFlags = ImGuiDebugLogFlags_OutputToTTY | ((_registry.isDevLogging())
                                            ? ImGuiDebugLogFlags_EventMask_
                                            : ImGuiDebugLogFlags_EventViewport);

  // Setup Dear ImGui style
  ImGuiStyle& style =
      ImGui::GetStyle ( );
  SKIF_ImGui_SetStyle (&style);

#if 0
  // When viewports are enabled we tweak WindowRounding/WindowBg
  //   so platform windows can look identical to regular ones.

  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    style.WindowRounding               = 5.0F;
    style.Colors [ImGuiCol_WindowBg].w = 1.0F;
  }
#endif

  // Setup Platform/Renderer bindings
  PLOG_INFO << "Initializing ImGui Win32 platform...";
  ImGui_ImplWin32_Init (nullptr); // This sets up a separate window/hWnd as well, though it will first be created at the end of the main loop
  PLOG_INFO << "Initializing ImGui D3D11 platform...";
  ImGui_ImplDX11_Init  (SKIF_pd3dDevice, SKIF_pd3dDeviceContext);

  //SKIF_Util_GetMonitorHzPeriod (SKIF_hWnd, MONITOR_DEFAULTTOPRIMARY, dwDwmPeriod);
  //OutputDebugString((L"Initial refresh rate period: " + std::to_wstring (dwDwmPeriod) + L"\n").c_str());

  // Message queue/pump
  MSG msg = { };

  // Variables related to the display SKIF is visible on
  ImVec2  windowPos;
  ImRect  windowRect       = ImRect(0.0f, 0.0f, 0.0f, 0.0f);
  ImRect  monitor_extent   = ImRect(0.0f, 0.0f, 0.0f, 0.0f);
  RepositionSKIF   = (! PathFileExistsW (L"SKIV.ini") || _registry.bOpenAtCursorPosition);

  // Add the status bar if it is not disabled
  SKIF_ImGui_AdjustAppModeSize (NULL);

  // Initialize ImGui fonts
  PLOG_INFO << "Initializing ImGui fonts...";
  SKIF_ImGui_InitFonts (SKIF_ImGui_FontSizeDefault, (! _Signal.Launcher && ! _Signal.LauncherURI && ! _Signal.Quit && ! _Signal.ServiceMode) );

  // Variable related to continue/pause rendering behaviour
  bool HiddenFramesContinueProcessing = true;  // We always have hidden frames that require to continue processing on init
  int  HiddenFramesRemaining         = 0;
  bool svcTransitionFromPendingState = false; // This is used to continue processing if we transitioned over from a pending state (which kills the refresh timer)

  bool repositionToCenter = false;

  // Do final checks and actions if we are expected to live longer than a few seconds
  if (! _Signal.Launcher && ! _Signal.LauncherURI && ! _Signal.Quit && ! _Signal.ServiceMode)
  {
    // Register HDR toggle hotkey (if applicable)
    SKIF_Util_RegisterHotKeyHDRToggle ( );

    // Register service (auto-stop) hotkey
    //SKIF_Util_RegisterHotKeySVCTemp   ( );
  }

  SKIF_Util_RegisterHotKeySnip ( );

  // Register the HTML Format for the clipboard
  CF_HTML = RegisterClipboardFormatW (L"HTML Format");
  PLOG_VERBOSE << "Clipboard registered format for 'HTML Format' is... " << CF_HTML;

  PLOG_INFO << "Initializing updater...";
  // Initialize the updater
  static SKIF_Updater& _updater = 
         SKIF_Updater::GetInstance ( );

  // Main loop
  PLOG_INFO << "Entering main loop...";
  SKIF_firstFrameTime = SKIF_Util_timeGetTime1 ( );

  while (! SKIF_Shutdown.load() ) // && IsWindow (hWnd) )
  {
    // Reset on each frame
    SKIF_MouseDragMoveAllowed = true;
    imageFadeActive           = false; // Assume there's no cover fade effect active
    msg                       = { };
    static UINT uiLastMsg     = 0x0;

#ifdef DEBUG

    // When built in debug mode, we should check if a debugger has been attached
    //   on every frame to identify and output to debuggers attached later
    if (! SKIF_debuggerPresent)
    {
      BOOL isRemoteDebuggerPresent = FALSE;
      CheckRemoteDebuggerPresent (SKIF_Util_GetCurrentProcess(), &isRemoteDebuggerPresent);

      if (isRemoteDebuggerPresent || IsDebuggerPresent())
      {
        static plog::DebugOutputAppender<plog::TxtFormatter> debugOutputAppender;
        plog::get()->addAppender (&debugOutputAppender);
        SKIF_debuggerPresent = true;
      }
    }

#endif // DEBUG

    // Various hotkeys that SKIF supports (resets on every frame)
    bool hotkeyF1    = (              ImGui::GetKeyData (ImGuiKey_F1    )->DownDuration == 0.0f), // Switch to Viewer
         hotkeyF2    = (              ImGui::GetKeyData (ImGuiKey_F2    )->DownDuration == 0.0f), // Switch to Settings
         hotkeyF3    = (              ImGui::GetKeyData (ImGuiKey_F3    )->DownDuration == 0.0f), // Switch to About
         hotkeyF5    = (              ImGui::GetKeyData (ImGuiKey_F5    )->DownDuration == 0.0f), // Settings/About: Refresh data
         hotkeyF6    = (              ImGui::GetKeyData (ImGuiKey_F6    )->DownDuration == 0.0f), // Appearance: Toggle DPI scaling
         hotkeyF7    = (              ImGui::GetKeyData (ImGuiKey_F7    )->DownDuration == 0.0f), // Appearance: Cycle between color themes
         hotkeyF8    = (              ImGui::GetKeyData (ImGuiKey_F8    )->DownDuration == 0.0f), // Appearance: Toggle UI borders
         hotkeyF9    = (              ImGui::GetKeyData (ImGuiKey_F9    )->DownDuration == 0.0f), // Appearance: Toggle color depth
         hotkeyF11   = (              ImGui::GetKeyData (ImGuiKey_F11   )->DownDuration == 0.0f), // Toggle Fullscreen Mode
         hotkeyEsc   = (              ImGui::GetKeyData (ImGuiKey_Escape)->DownDuration == 0.0f), // Close the app
         hotkeyCtrlQ = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_Q     )->DownDuration == 0.0f), // Close the app
         hotkeyCtrlR = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_R     )->DownDuration == 0.0f), // Library/About: Refresh data
         hotkeyCtrlO = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_O     )->DownDuration == 0.0f), // Viewer: Open File
         hotkeyCtrlA = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_A     )->DownDuration == 0.0f), // Viewer: Open File
         hotkeyCtrlD = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_D     )->DownDuration == 0.0f), // Viewer: Toggle Image Details
         hotkeyCtrlF = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_F     )->DownDuration == 0.0f), // Toggle Fullscreen Mode
         hotkeyCtrlV = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_V     )->DownDuration == 0.0f), // Paste data through the clipboard
         hotkeyCtrlN = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_N     )->DownDuration == 0.0f), // Minimize app
         hotkeyCtrlS = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_S     )->DownDuration == 0.0f), // Save Current Image (in same Dynamic Range)
         hotkeyCtrlX = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_X     )->DownDuration == 0.0f); // Export Current Image (HDR -> SDR)

    // No more compiler warnings dammit!
    std::ignore = hotkeyF5;
    std::ignore = hotkeyCtrlR;

    // Handled in viewer.cpp
       //hotkeyCtrl1 = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_1     )->DownDuration == 0.0f), // Viewer -> Image Scaling: View actual size (1:1 / None)
       //hotkeyCtrl2 = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_2     )->DownDuration == 0.0f), // Viewer -> Image Scaling: Zoom to fit (Fit)
       //hotkeyCtrl3 = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_3     )->DownDuration == 0.0f); // Viewer -> Image Scaling: Fill the window (Fill)
       //hotkeyCtrlW = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_W     )->DownDuration == 0.0f), // Close the app

    auto _TranslateAndDispatch = [&](void) -> bool
    {
      while (! SKIF_Shutdown.load() && PeekMessage (&msg, 0, 0U, 0U, PM_REMOVE))
      {
        if (msg.message == WM_QUIT)
        {
          SKIF_Shutdown.store (true);
          SKIF_ExitCode = (int) msg.wParam;
          return false; // return false on exit or system shutdown
        }

        // There are four different window procedures that a message can be dispatched to based on the HWND of the message
        // 
        //                           SKIF_Notify_WndProc ( )  <=  SKIF_Notify_hWnd                         :: Handles messages meant for the notification icon.
        //                                  SKIF_WndProc ( )  <=  SKIF_Notify_hWnd                         :: Handles all custom SKIF window messages and actions.
        //                                                                                                    - Gets called by SKIF_Notify_WndProc ( ).
        // 
        // ImGui_ImplWin32_WndProcHandler_PlatformWindow ( )  <=  SKIF_ImGui_hWnd, Other HWNDs             :: Handles messages meant for the overarching ImGui Platform window of SKIF, as well as any
        //                                                                                                      additional swapchain windows (menus/tooltips that stretches beyond SKIF_ImGui_hWnd).
        // ImGui_ImplWin32_WndProcHandler                ( )  <=  SKIF_ImGui_hWnd, Other HWNDs             :: Handles mouse/key input and focus events for ImGui platform windows.
        //                                                                                                    - Gets called by ImGui_ImplWin32_WndProcHandler_PlatformWindow ( ).
        // 
        TranslateMessage (&msg);
        DispatchMessage  (&msg);

        if (msg.hwnd == 0) // Don't redraw on thread messages
          msgDontRedraw = true;

        if (msg.message == WM_MOUSEMOVE)
        {
          static LPARAM lParamPrev;
          static WPARAM wParamPrev;

          // Workaround for a bug in System Informer where it sends a fake WM_MOUSEMOVE message to the window the cursor is over
          // Ignore the message if WM_MOUSEMOVE has identical data as the previous msg
          if (msg.lParam == lParamPrev &&
              msg.wParam == wParamPrev)
            msgDontRedraw = true;
          else {
            lParamPrev = msg.lParam;
            wParamPrev = msg.wParam;
          }
        }

        uiLastMsg = msg.message;
      }

      return ! SKIF_Shutdown.load(); // return false on exit or system shutdown
    };

    // Apply any changes to the ImGui style
    // Do it at the beginning of frames to prevent ImGui::Push... from affecting the styling
    // Note that Win11 rounded border color won't be applied until after a restart
      
    // F7 to cycle between color themes
    if ( (_registry.iStyleTemp != _registry.iStyle) || hotkeyF7)
    {
      _registry.iStyle            = (_registry.iStyleTemp != _registry.iStyle)
                                  ?  _registry.iStyleTemp
                                  : (_registry.iStyle + 1) % UIStyle_COUNT;
      _registry.regKVStyle.putData  (_registry.iStyle);

      ImGuiStyle            newStyle;
      SKIF_ImGui_SetStyle (&newStyle);

      _registry.iStyleTemp = _registry.iStyle;

      extern void
        SKIF_ImGui_ImplWin32_UpdateDWMBorders (void);
        SKIF_ImGui_ImplWin32_UpdateDWMBorders (    );
    }

#if 0
    // Registry watch to check if snapping/drag from window settings has changed in Windows
    // No need to for SKIF to wake up on changes when unfocused, so skip having it be global
    static SKIF_RegistryWatch
      dwmWatch ( HKEY_CURRENT_USER,
                   LR"(Control Panel\Desktop)",
                     L"WinDesktopNotify", FALSE, REG_NOTIFY_CHANGE_LAST_SET, UITab_None, false, true);

    // When the registry is changed, update our internal state accordingly
    if (dwmWatch.isSignaled ( ))
    {
      _registry.bMaximizeOnDoubleClick   =
        SKIF_Util_GetDragFromMaximized (true)                // IF the OS prerequisites are enabled
        ? _registry.regKVMaximizeOnDoubleClick.hasData ( )   // AND we have data in the registry
          ? _registry.regKVMaximizeOnDoubleClick.getData ( ) // THEN use the data,
          : true                                             // otherwise default to true,
        : false;                                             // and false if OS prerequisites are disabled
    }
#endif

    // F6 to toggle DPI scaling
    if (changedHiDPIScaling || hotkeyF6)
    {
      // We only change bDPIScaling if ImGui::Checkbox (settings tab) was not used,
      //   as otherwise it have already been changed to reflect its new value
      if (! changedHiDPIScaling)
        _registry.bDPIScaling =        ! _registry.bDPIScaling;
      _registry.regKVDPIScaling.putData (_registry.bDPIScaling);

      changedHiDPIScaling = false;

      // Reset reduced height
      SKIF_vecAlteredSize.y = 0.0f;

      // Take the current display into account
      HMONITOR monitor =
        ::MonitorFromWindow (SKIF_ImGui_hWnd, MONITOR_DEFAULTTONEAREST);
        
      SKIF_ImGui_GlobalDPIScale = (_registry.bDPIScaling) ? ImGui_ImplWin32_GetDpiScaleForMonitor (monitor) : 1.0f;

      ImGuiStyle              newStyle;
      SKIF_ImGui_SetStyle   (&newStyle);

      SKIF_ImGui_AdjustAppModeSize (monitor);

      LONG_PTR lStyle = GetWindowLongPtr (SKIF_ImGui_hWnd, GWL_STYLE);
      if (lStyle & WS_MAXIMIZE)
        repositionToCenter   = true;
      else
        RespectMonBoundaries = true;
    }

    // F8 to toggle UI borders
    if (hotkeyF8)
    {
      _registry.bUIBorders = ! _registry.bUIBorders;
      _registry.regKVUIBorders.putData (_registry.bUIBorders);

      ImGuiStyle            newStyle;
      SKIF_ImGui_SetStyle (&newStyle);
    }

    // F9 to cycle between color depths
    if (hotkeyF9)
    {
      if (_registry.iHDRMode > 0 && SKIF_Util_IsHDRActive())
        _registry.iHDRMode = 1 + (_registry.iHDRMode % 2); // Cycle between 1 (10 bpc) and 2 (16 bpc)
      else 
        _registry.iSDRMode = (_registry.iSDRMode + 1) % 3; // Cycle between 0 (8 bpc), 1 (10 bpc), and 2 (16 bpc)

      RecreateSwapChains = true;
    }

    // F11 / Ctrl+F toggles fullscreen mode
    if (hotkeyF11 || hotkeyCtrlF)
      SKIF_ImGui_SetFullscreen (SKIF_ImGui_hWnd, ! SKIF_ImGui_IsFullscreen (SKIF_ImGui_hWnd));

    if (hotkeyCtrlD)
    {
      _registry.bImageDetails = ! _registry.bImageDetails;
      _registry.regKVImageDetails.putData (_registry.bImageDetails);
    }

    // Should we invalidate the fonts and/or recreate them?

    if (SKIF_ImGui_GlobalDPIScale != SKIF_ImGui_GlobalDPIScale_Last)
      invalidateFonts = true;

    SKIF_ImGui_GlobalDPIScale_Last = SKIF_ImGui_GlobalDPIScale;
    float fontScale = 18.0F * SKIF_ImGui_GlobalDPIScale;
    if (fontScale < 15.0F)
      fontScale += 1.0F;

    if (invalidateFonts)
    {
      invalidateFonts = false;
      SKIF_ImGui_InvalidateFonts ( );
    }
    
    // This occurs on the next frame, as failedLoadFonts gets evaluated and set as part of ImGui_ImplDX11_NewFrame
    else if (failedLoadFonts)
    {
      // This scenario should basically never happen nowadays that SKIF only loads the specific characters needed from each character set

      SKIF_ImGui_InvalidateFonts ( );

      failedLoadFonts       = false;
      failedLoadFontsPrompt = true;
    }

    //temp_time = SKIF_Util_timeGetTime1();
    //PLOG_INFO << "Operation took " << (temp_time - SKIF_Util_timeGetTime1()) << " ms.";

#pragma region New UI Frame
    
    extern bool
      SKIF_ImGui_ImplWin32_WantUpdateMonitors (void);
    bool _WantUpdateMonitors =
      SKIF_ImGui_ImplWin32_WantUpdateMonitors (    );

    // Reset the state of tracked scrollbars
    //SKIF_ImGui_UpdateScrollbarState ( );

    // Start the Dear ImGui frame
    ImGui_ImplDX11_NewFrame  (); // (Re)create individual swapchain windows
    ImGui_ImplWin32_NewFrame (); // Handle input
    ImGui::NewFrame          ();
    {
      SKIF_FrameCount.store(ImGui::GetFrameCount());

      ImRect rectCursorMonitor; // RepositionSKIF

      // RepositionSKIF -- Step 1: Retrieve monitor of cursor
      if (RepositionSKIF)
      {
        ImRect t;
        for (int monitor_n = 0; monitor_n < ImGui::GetPlatformIO().Monitors.Size; monitor_n++)
        {
          const ImGuiPlatformMonitor& tmpMonitor = ImGui::GetPlatformIO().Monitors[monitor_n];
          t = ImRect(tmpMonitor.MainPos, (tmpMonitor.MainPos + tmpMonitor.MainSize));

          POINT               mouse_screen_pos = { };
          if (::GetCursorPos (&mouse_screen_pos))
          {
            ImVec2 os_pos = ImVec2( (float)mouse_screen_pos.x,
                                    (float)mouse_screen_pos.y );
            if (t.Contains (os_pos))
            {
              rectCursorMonitor = t;
              SKIF_ImGui_GlobalDPIScale = (_registry.bDPIScaling) ? tmpMonitor.DpiScale : 1.0f;
            }
          }
        }
      }
      
      SKIF_vecRegularMode     = SKIF_vecRegularModeAdjusted * SKIF_ImGui_GlobalDPIScale;
      
      SKIF_vecRegularMode.y  -= SKIF_vecAlteredSize.y;

      SKIF_vecCurrentMode     = SKIF_vecRegularMode ;

      ImGui::SetNextWindowClass (&SKIF_AppWindow);

      // RepositionSKIF -- Step 2: Repositon the window
      // Repositions the window in the center of the monitor the cursor is currently on
      if (RepositionSKIF)
        ImGui::SetNextWindowPos (ImVec2(rectCursorMonitor.GetCenter().x - (SKIF_vecCurrentMode.x / 2.0f), rectCursorMonitor.GetCenter().y - (SKIF_vecCurrentMode.y / 2.0f)));

      // Calculate new window boundaries and changes to fit within the workspace if it doesn't fit
      //   Delay running the code to on the third frame to allow other required parts to have already executed...
      //     Otherwise window gets positioned wrong on smaller monitors !
      if (RespectMonBoundaries && ImGui::GetFrameCount() > 2)
      {   RespectMonBoundaries = false;

        ImVec2 topLeft      = windowPos,
               bottomRight  = windowPos + SKIF_vecCurrentMode,
               newWindowPos = windowPos;

        if (      topLeft.x < monitor_extent.Min.x )
             newWindowPos.x = monitor_extent.Min.x;
        if (      topLeft.y < monitor_extent.Min.y )
             newWindowPos.y = monitor_extent.Min.y;

        if (  bottomRight.x > monitor_extent.Max.x )
             newWindowPos.x = monitor_extent.Max.x - SKIF_vecCurrentMode.x;
        if (  bottomRight.y > monitor_extent.Max.y )
             newWindowPos.y = monitor_extent.Max.y - SKIF_vecCurrentMode.y;

        if ( newWindowPos.x != windowPos.x ||
             newWindowPos.y != windowPos.y )
          ImGui::SetNextWindowPos (newWindowPos);
      }

      // If toggling mode when maximized, we need to reposition the window
      if (repositionToCenter)
      {   repositionToCenter = false;
        ImGui::SetNextWindowPos  (monitor_extent.GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));
      }

      // Resize app window based on the image resolution
      extern bool tryingToLoadImage;
      bool resizeAppWindow = (_registry.bAdjustWindow && SKIF_ImGui_hWnd != NULL && ! SKIF_ImGui_IsFullscreen (SKIF_ImGui_hWnd))
                           ? (! tryingToLoadImage && SKIV_ResizeApp.x != 0.0f && ! (GetWindowLongPtr (SKIF_ImGui_hWnd, GWL_STYLE) & WS_MAXIMIZE))
                           : false;

      if (resizeAppWindow)
      {

#if 0
        ImVec2 size_maximum = monitor_extent.GetSize() * 0.8f;
        float contentAspectRatio = SKIV_ResizeApp.x / SKIV_ResizeApp.y;

        if (size_maximum.x < SKIV_ResizeApp.x)
        {
          SKIV_ResizeApp.x = size_maximum.x;
          SKIV_ResizeApp.y = SKIV_ResizeApp.x / contentAspectRatio;
        }

        if (size_maximum.y < SKIV_ResizeApp.y)
        {
          SKIV_ResizeApp.y = size_maximum.y;
          SKIV_ResizeApp.x = SKIV_ResizeApp.y * contentAspectRatio;
        }
#endif

        ImVec2 size_maximum = monitor_extent.GetSize();

        PLOG_VERBOSE << "SKIV_ResizeApp: " << SKIV_ResizeApp.x << "x" << SKIV_ResizeApp.y;

        if (size_maximum.x < SKIV_ResizeApp.x || size_maximum.y < SKIV_ResizeApp.y)
        {
          SKIF_ImGui_SetFullscreen (SKIF_ImGui_hWnd, ! SKIF_ImGui_IsFullscreen (SKIF_ImGui_hWnd));
          resizeAppWindow = false;
        }

        else
          ImGui::SetNextWindowSize (SKIV_ResizeApp);

        SKIV_ResizeApp = ImVec2 (0.0f, 0.0f);
      }

      static const ImVec2 wnd_minimum_size = ImVec2 (200.0f, 200.0f) * SKIF_ImGui_GlobalDPIScale;

      // The first time SKIF is being launched, or repositioned on launch, use a higher minimum size
      if (ImGui::GetFrameCount() == 1 && RepositionSKIF)
        ImGui::SetNextWindowSizeConstraints (wnd_minimum_size * 2.0f, ImVec2 (FLT_MAX, FLT_MAX));

      // On the second frame, limit the initial window size to only 80% of the monitor size
      /*
      else if (resizeAppWindow || ImGui::GetFrameCount() == 2)
      {
        ImVec2 size_current = windowRect.GetSize();
        ImVec2 size_maximum = monitor_extent.GetSize() * 0.8f;

        ImGui::SetNextWindowSizeConstraints (wnd_minimum_size, size_maximum);

        // If the window size was too large, we need to reposition the window to the center as well
        // This is handled on the next frame by the code above us
        if (size_current.x > size_maximum.x || size_current.y > size_maximum.y)
          repositionToCenter = true;
      }
      */

      // The rest of the frames are uncapped
      else
        ImGui::SetNextWindowSizeConstraints (wnd_minimum_size, ImVec2 (FLT_MAX, FLT_MAX));

      ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2());
      ImGui::PushStyleVar (ImGuiStyleVar_WindowBorderSize, 0.0f); // Disable ImGui's 1 px window border
      ImGui::Begin (SKIV_WINDOW_TITLE_HASH,
                       nullptr,
                       //ImGuiWindowFlags_NoResize          |
                         ImGuiWindowFlags_NoCollapse        |
                         ImGuiWindowFlags_NoTitleBar        |
                         ImGuiWindowFlags_NoScrollbar       | // Hide the scrollbar for the main window
                         ImGuiWindowFlags_NoScrollWithMouse | // Prevent scrolling with the mouse as well
           (io.KeyCtrl ? ImGuiWindowFlags_NoMove : ImGuiWindowFlags_None)              // This was added in #8bf06af, but I am unsure why.
                      // The only comment is that it was DPI related? This prevents Ctrl+Tab from moving the window so must not be used
      );
      ImGui::PopStyleVar (2);

      SK_RunOnce (ImGui::GetCurrentWindow()->HiddenFramesCannotSkipItems += 2);

      HiddenFramesRemaining         = ImGui::GetCurrentWindowRead()->HiddenFramesCannotSkipItems;
      HiddenFramesContinueProcessing = (HiddenFramesRemaining > 0);
      HoverTipActive = false;

      extern ImGuiPlatformMonitor*
        SKIF_ImGui_ImplWin32_GetPlatformMonitorProxy (ImGuiViewport* viewport, bool center);
      ImGuiPlatformMonitor* actMonitor =
        SKIF_ImGui_ImplWin32_GetPlatformMonitorProxy (ImGui::GetWindowViewport ( ), true);

      // Crop the window on resolutions with a height smaller than what SKIF requires
      if (actMonitor != nullptr)
      {
        static ImGuiPlatformMonitor* preMonitor = nullptr;

        // Only act once at launch or if we are, in fact, on a new display
        if (preMonitor != actMonitor || _WantUpdateMonitors)
        {
          // Reset reduced height
          SKIF_vecAlteredSize.y = 0.0f;

          // This is only necessary to run once on launch, to account for the startup display DPI
          SK_RunOnce (SKIF_ImGui_GlobalDPIScale = (_registry.bDPIScaling) ? ImGui::GetWindowViewport()->DpiScale : 1.0f);

          ImVec2 tmpCurrentSize  = SKIF_vecRegularModeAdjusted ;

          if (tmpCurrentSize.y * SKIF_ImGui_GlobalDPIScale > (actMonitor->WorkSize.y))
            SKIF_vecAlteredSize.y = (tmpCurrentSize.y * SKIF_ImGui_GlobalDPIScale - (actMonitor->WorkSize.y)); // (actMonitor->WorkSize.y - 50.0f)

          // Also recreate the swapchain (applies any HDR/SDR changes between displays)
          //   but not the first time to prevent unnecessary swapchain recreation on launch
          if (preMonitor != nullptr)
            RecreateSwapChains = true;

          preMonitor = actMonitor;
        }
      }

      // RepositionSKIF -- Step 3: The Final Step -- Prevent the global DPI scale from potentially being set to outdated values
      if (RepositionSKIF)
        RepositionSKIF = false;

      // Only allow navigational hotkeys when in Large Mode and as long as no popups are opened
      if (! SKIF_ImGui_IsAnyPopupOpen ( ))
      {
        if (hotkeyF1)
        {
          if (SKIF_Tab_Selected != UITab_Viewer)
              SKIF_Tab_ChangeTo  = UITab_Viewer;
        }

        if (hotkeyF2)
        {
          if (SKIF_Tab_Selected != UITab_Settings)
              SKIF_Tab_ChangeTo  = UITab_Settings;
        }

        if (hotkeyF3)
        {
          if (SKIF_Tab_Selected != UITab_About)
              SKIF_Tab_ChangeTo  = UITab_About;
        }

        if (allowShortcutCtrlA && (hotkeyCtrlA || hotkeyCtrlO))
        {
          if (SKIF_Tab_Selected != UITab_Viewer)
              SKIF_Tab_ChangeTo  = UITab_Viewer;

          OpenFileDialog = PopupState_Open;
        }

        if (allowShortcutCtrlA && (hotkeyCtrlX || hotkeyCtrlS))
        {
          if (SKIF_Tab_Selected != UITab_Viewer)
              SKIF_Tab_ChangeTo  = UITab_Viewer;

          if (hotkeyCtrlX)
            ExportSDRDialog = PopupState_Open;
          if (hotkeyCtrlS)
            SaveFileDialog = PopupState_Open;
        }
      }

      allowShortcutCtrlA = true;



      // Escape does situational stuff
      if (hotkeyEsc)
      {
        if (PopupMessageInfo != PopupState_Closed)
          SKIF_ImGui_PopBackInfoPopup ( );

        else if
          (UpdatePromptPopup != PopupState_Closed ||
           HistoryPopup      != PopupState_Closed  )
        {
          UpdatePromptPopup   = PopupState_Closed;
          HistoryPopup        = PopupState_Closed;

          ImGui::ClosePopupsOverWindow (ImGui::GetCurrentWindowRead ( ), false);
        }

        else
        {
          switch (SKIF_Tab_Selected)
          {
          case UITab_None:
            break;
          case UITab_Viewer:
            if (SKIF_ImGui_IsFullscreen (SKIF_ImGui_hWnd))
              SKIF_ImGui_SetFullscreen  (SKIF_ImGui_hWnd, false);
            else
              bKeepWindowAlive = false;
            break;
          case UITab_Settings:
            SKIF_Tab_ChangeTo = UITab_Viewer;
            break;
          case UITab_About:
            break;
          case UITab_ALL:
            break;
          default:
            break;
          }
        }
      }

      ImGui::BeginGroup ();

      // Begin Snipping Mode
      if (_registry._SnippingMode)
      {
#pragma region UI: Snipping Mode

        if (ImGui::Button ("turn off snipping mode"))
        {
          _registry._SnippingMode = ! _registry._SnippingMode;
        }

#pragma endregion

      }

      // Begin Large Mode
      else
      {
#pragma region UI: Large Mode

        // TAB: Viewer
        if (SKIF_Tab_Selected == UITab_Viewer ||
            SKIF_Tab_ChangeTo == UITab_Viewer)
        {
          ImGui::PushStyleVar (ImGuiStyleVar_FramePadding, ImVec2());
          bool show = SKIF_ImGui_BeginMainChildFrame (ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
          ImGui::PopStyleVar  ( );

          /*
          if (! _registry.bFirstLaunch)
          {
            // Select the About tab on first launch
            _registry.bFirstLaunch = ! _registry.bFirstLaunch;
            SKIF_Tab_ChangeTo = UITab_About;

            // Store in the registry so this only occur once.
            _registry.regKVFirstLaunch.putData(_registry.bFirstLaunch);
          }
          */

          extern float fTint;
          if (SKIF_Tab_ChangeTo == UITab_Viewer)
          {
            // Reset the dimmed cover when going back to the tab
            if (_registry.iDarkenImages == 2)
              fTint = 0.75f;
          }

          if (show)
          {
            SKIF_UI_Tab_DrawViewer ( );
          
            SKIF_ImGui_AutoScroll  (true, SKIF_ImGuiAxis_Both);
            SKIF_ImGui_UpdateScrollbarState ( );

            ImGui::EndChild        ( );
          }

          if (SKIF_Tab_ChangeTo == UITab_Viewer)
          {
            PLOG_DEBUG << "Switched to tab: Library";
            SKIF_Tab_Selected = UITab_Viewer;
            SKIF_Tab_ChangeTo = UITab_None;
          }
        }

        // Change to Viewer tab on the next frame if a drop occurs on another tab
        else if (! dragDroppedFilePath.empty()) {
          SKIF_Tab_ChangeTo = UITab_Viewer;
        }


        // TAB: Settings
        if (SKIF_Tab_Selected == UITab_Settings ||
            SKIF_Tab_ChangeTo == UITab_Settings)

        {
          // Refresh things when visiting from another tab or when forced
          if (SKIF_Tab_ChangeTo == UITab_Settings || RefreshSettingsTab)
            SKIF_Util_IsHDRActive (true);

          ImGui::PushStyleVar (ImGuiStyleVar_FramePadding, ImVec2 (15.0f, 15.0f) * SKIF_ImGui_GlobalDPIScale);
          bool show = SKIF_ImGui_BeginMainChildFrame ( );
          ImGui::PopStyleVar  ( );

          if (show)
          {
            SKIF_UI_Tab_DrawSettings ( );

            SKIF_ImGui_AutoScroll  (true, SKIF_ImGuiAxis_Both);
            SKIF_ImGui_UpdateScrollbarState ( );

            ImGui::EndChild        ( );
          }

          if (SKIF_Tab_ChangeTo == UITab_Settings)
          {
            PLOG_DEBUG << "Switched to tab: Settings";
            SKIF_Tab_Selected  = UITab_Settings;
            SKIF_Tab_ChangeTo  = UITab_None;
            RefreshSettingsTab = false;

          }
        }


#if 0
        // TAB: About
        if (SKIF_Tab_Selected == UITab_About ||
            SKIF_Tab_ChangeTo == UITab_About)
        {
          SKIF_ImGui_BeginMainChildFrame ( );

          SKIF_UI_Tab_DrawAbout( );

          // Engages auto-scroll mode (left click drag on touch + middle click drag on non-touch)
          SKIF_ImGui_AutoScroll  (true);

          if (SKIF_Tab_ChangeTo == UITab_About)
          {
            PLOG_DEBUG << "Switched to tab: About";
            SKIF_Tab_Selected = UITab_About;
            SKIF_Tab_ChangeTo = UITab_None;

          }

          ImGui::EndChild         ( );
        }
#endif

#pragma endregion
      }

      ImGui::EndGroup             ( );

#pragma region Shelly the Ghost

      if (_registry.bGhost)
      {
        ImVec2 shelly_movable_area = ImVec2 (
           wnd_minimum_size.x * SKIF_ImGui_GlobalDPIScale,
           ImGui::CalcTextSize (ICON_FA_GHOST).y + 4.0f * SKIF_ImGui_GlobalDPIScale
        );

        ImGui::SetCursorPos (ImVec2 (
            (ImGui::GetWindowContentRegionMax().x - shelly_movable_area.x) / 2.0f,
             10.0f * SKIF_ImGui_GlobalDPIScale
        ));
        
        ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2());
        ImGui::PushStyleVar (ImGuiStyleVar_FramePadding,  ImVec2());
        bool shelly_show = ImGui::BeginChild ("###SKIV_SHELLY", shelly_movable_area, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar  (2);

        if (shelly_show)
        {
          SKIF_UI_DrawShellyTheGhost ( );

          ImGui::EndChild ( );
        }
      }

#pragma endregion

#pragma region WindowButtons

      // Top right window buttons

      if (_registry.bUICaptionButtons)
      {
        ImVec2 window_btn_size = ImVec2 (
          68.0f * SKIF_ImGui_GlobalDPIScale,
          24.0f * SKIF_ImGui_GlobalDPIScale
        );

        ImVec2 prevCursorPos =
          ImGui::GetCursorPos ();

        constexpr float distanceFromEdge = 6.0f;
        float distanceFromScrollbar = (SKIF_ImGui_IsScrollbarY()) ? ImGui::GetStyle().ScrollbarSize : 0.0f;

        ImGui::SetCursorPos (
          ImVec2 ( (ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().FrameBorderSize * 2 - window_btn_size.x - ((distanceFromEdge + distanceFromScrollbar) * SKIF_ImGui_GlobalDPIScale)),
                     ((distanceFromEdge * SKIF_ImGui_GlobalDPIScale) - ImGui::GetStyle().FrameBorderSize * 2) )
        );

        if (ImGui::BeginChild ("###SKIV_WINDOW_BUTTONS", window_btn_size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground))
        {
          ImGui::PushStyleVar (
            ImGuiStyleVar_FrameRounding, 25.0f * SKIF_ImGui_GlobalDPIScale
          );

          if (ImGui::Button (ICON_FA_WINDOW_MINIMIZE, ImVec2 ( 30.0f * SKIF_ImGui_GlobalDPIScale, 0.0f )))
            hotkeyCtrlN = true;

          ImGui::SameLine ();

          ImGui::PushStyleColor   (ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Failure));
          ImGui::PushStyleColor   (ImGuiCol_ButtonActive,  ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Failure) * ImVec4(1.2f, 1.2f, 1.2f, 1.0f));

          static bool closeButtonHoverActive = false;

          if (_registry._StyleLightMode && closeButtonHoverActive)
            ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg)); //ImVec4 (0.9F, 0.9F, 0.9F, 1.0f));

          if (ImGui::Button (ICON_FA_XMARK, ImVec2 ( 30.0f * SKIF_ImGui_GlobalDPIScale, 0.0f ) )) // HotkeyEsc is situational
            hotkeyCtrlQ = true;
      
          if (_registry._StyleLightMode)
          {
            if (closeButtonHoverActive)
              ImGui::PopStyleColor ( );
          
            closeButtonHoverActive = (ImGui::IsItemHovered () || ImGui::IsItemActivated ());
          }

          ImGui::PopStyleColor (2);

          ImGui::PopStyleVar ();

          ImGui::EndChild ( );
        }

        ImGui::SetCursorPos (prevCursorPos);

        ImGui::Dummy (ImVec2 (0, 0)); // Dummy required here to solve ImGui::ErrorCheckUsingSetCursorPosToExtendParentBoundaries()
      }

      // End of top right window buttons

#pragma endregion

#pragma region CaptionActions

      if (hotkeyCtrlN)
        ShowWindow (SKIF_ImGui_hWnd, SW_MINIMIZE);

      if (hotkeyCtrlQ || bKeepWindowAlive == false) // HotkeyEsc is situational
        bKeepProcessAlive = false;

#pragma endregion


      // Process any existing message popups
      extern void
        SKIF_ImGui_InfoMessage_Process (void);
      SKIF_ImGui_InfoMessage_Process ( );


      // Handle the update popup

      static std::wstring updateRoot = SK_FormatStringW (LR"(%ws\Version\)", _path_cache.skiv_userdata);
      static float  UpdateAvailableWidth = 0.0f;

      // Only open the update prompt after the library has appeared (fixes the popup weirdly closing for some unknown reason)
      if (UpdatePromptPopup == PopupState_Open && ! HiddenFramesContinueProcessing && ! SKIF_ImGui_IsAnyPopupOpen ( ) && ! ImGui::IsMouseDragging (ImGuiMouseButton_Left))
      {
        //UpdateAvailableWidth = ImGui::CalcTextSize ((SK_WideCharToUTF8 (newVersion.description) + " is ready to be installed.").c_str()).x + 3 * ImGui::GetStyle().ItemSpacing.x;
        UpdateAvailableWidth = 360.0f;

        // 8.0f  per character
        // 25.0f for the scrollbar
        float calculatedWidth = static_cast<float>(_updater.GetResults().release_notes_formatted.max_length) * 8.0f + 25.0f;

        if (calculatedWidth > UpdateAvailableWidth)
          UpdateAvailableWidth = calculatedWidth;

        UpdateAvailableWidth = std::min<float> (UpdateAvailableWidth, SKIF_vecCurrentMode.x * 0.9f);

        ImGui::OpenPopup ("###UpdatePrompt");
      }

      // Update Available prompt
      // 730px    - Full popup width
      // 715px    - Release Notes width
      //  15px    - Approx. scrollbar width
      //   7.78px - Approx. character width (700px / 90 characters)

      if (UpdatePromptPopup == PopupState_Open ||
          UpdatePromptPopup == PopupState_Opened)
      {
        ImGui::SetNextWindowSize (ImVec2 (UpdateAvailableWidth * SKIF_ImGui_GlobalDPIScale, 0.0f));
        ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));
      }

      if (ImGui::BeginPopupModal ( "Version Available###UpdatePrompt", nullptr,
                                     ImGuiWindowFlags_NoResize         |
                                     ImGuiWindowFlags_NoMove           |
                                     ImGuiWindowFlags_AlwaysAutoResize )
         )
      {
        std::string currentVersion = SKIV_VERSION_STR_A;
        std::string compareLabel;
        ImVec4      compareColor;
        bool        compareNewer = (SKIF_Util_CompareVersionStrings (_updater.GetResults().version, currentVersion) > 0);

        if (UpdatePromptPopup == PopupState_Open)
        {
          // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
          ImGuiWindow* window = ImGui::FindWindowByName ("###UpdatePrompt");
          if (window != nullptr && ! window->Appearing)
            UpdatePromptPopup = PopupState_Opened;
        }

        if (compareNewer)
        {
          compareLabel = "This version is newer than the currently installed.";
          compareColor = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success);
        }
        else
        {
          compareLabel = "WARNING: You are about to roll back Special K as this version is older than the currently installed!";
          compareColor = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning);
        }

        SKIF_ImGui_Spacing ();

        std::string updateTxt = "is ready to be installed.";

        if ((_updater.GetState ( ) & UpdateFlags_Downloaded) != UpdateFlags_Downloaded)
          updateTxt = "is available for download.";

        float fX = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(((_updater.GetResults().description) + updateTxt).c_str()).x + (((compareNewer) ? 2 : 1) * ImGui::GetStyle().ItemSpacing.x)) / 2;

        ImGui::SetCursorPosX(fX);

        ImGui::TextColored (compareColor, (_updater.GetResults().description).c_str());
        ImGui::SameLine ( );
        ImGui::Text (updateTxt.c_str());

        SKIF_ImGui_Spacing ();

        if ((_updater.GetState ( ) & UpdateFlags_Failed) == UpdateFlags_Failed)
        {
          ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning));
          ImGui::TextWrapped ("A failed download was detected! Click Download below to try again.");
          ImGui::Text        ("In case of continued errors, consult SKIF.log for more details.");
          ImGui::PopStyleColor  ( );

          SKIF_ImGui_Spacing ();
        }

        ImGui::Text     ("Target Folder:");
        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info));
        ImGui::TextWrapped    (SK_WideCharToUTF8 (_path_cache.skiv_install).c_str());
        ImGui::PopStyleColor  ( );

        SKIF_ImGui_Spacing ();

        if (!_updater.GetResults().release_notes_formatted.notes.empty())
        {
          if (! _updater.GetResults().description_installed.empty())
          {
            ImGui::Text           ("Changes from");
            ImGui::SameLine       ( );
            ImGui::TextColored    (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), (_updater.GetResults().description_installed + ":").c_str());
          }
          else {
            ImGui::Text           ("Changes:");
          }

          ImGui::PushStyleColor (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));
          ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));
          ImGui::PushFont       (fontConsolas);
          ImGui::InputTextEx    ( "###UpdatePromptChanges", "The update does not contain any release notes...",
                                    _updater.GetResults().release_notes_formatted.notes.data(),
                                      static_cast<int>(_updater.GetResults().release_notes_formatted.notes.size()),
                                        ImVec2 ( (UpdateAvailableWidth - 15.0f) * SKIF_ImGui_GlobalDPIScale,
                                          std::min<float>(
                                            std::min<float>(_updater.GetResults().history_formatted.lines, 40.0f) * fontConsolas->FontSize,
                                              SKIF_vecCurrentMode.y * 0.5f)
                                               ), ImGuiInputTextFlags_ReadOnly | static_cast<ImGuiInputTextFlags_>(ImGuiInputTextFlags_Multiline));

          SKIF_ImGui_DisallowMouseDragMove ( );

          ImGui::PopFont        ( );
          ImGui::PopStyleColor  ( );
          ImGui::PopStyleColor  ( );

          SKIF_ImGui_Spacing ();
        }

        fX = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(compareLabel.c_str()).x + (((compareNewer) ? 2 : 1) * ImGui::GetStyle().ItemSpacing.x)) / 2;

        ImGui::SetCursorPosX(fX);
          
        ImGui::TextColored (compareColor, compareLabel.c_str());

        SKIF_ImGui_Spacing ();

        fX = (ImGui::GetContentRegionAvail().x - (((compareNewer) ? 3 : 2) * 100 * SKIF_ImGui_GlobalDPIScale) - (((compareNewer) ? 2 : 1) * ImGui::GetStyle().ItemSpacing.x)) / 2;

        ImGui::SetCursorPosX(fX);

        std::string btnLabel = (compareNewer) ? "Update" : "Rollback";

        if ((_updater.GetState ( ) & UpdateFlags_Downloaded) != UpdateFlags_Downloaded)
          btnLabel = "Download";

        if (ImGui::Button (btnLabel.c_str(), ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                                       25 * SKIF_ImGui_GlobalDPIScale )))
        {
          if (btnLabel == "Download")
          {
            _registry.wsIgnoreUpdate = L"";
            _registry.regKVIgnoreUpdate.putData (_registry.wsIgnoreUpdate);

            // Trigger a new check for updates (which should download the installer)
            _updater.CheckForUpdates (false, ! compareNewer);
          }

          else {
            std::wstring args = SK_FormatStringW (LR"(/VerySilent /NoRestart /Shortcuts=false /DIR="%ws")", _path_cache.skiv_install);

            SKIF_Util_OpenURI (updateRoot + _updater.GetResults().filename, SW_SHOWNORMAL, L"OPEN", args.c_str());

            //Sleep(50);
            //bKeepProcessAlive = false;
          }

          UpdatePromptPopup = PopupState_Closed;
          ImGui::CloseCurrentPopup ();
        }

        SKIF_ImGui_DisallowMouseDragMove ( );

        ImGui::SameLine ();
        ImGui::Spacing  ();
        ImGui::SameLine ();

        if (compareNewer)
        {
          if (ImGui::Button ("Ignore", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                                 25 * SKIF_ImGui_GlobalDPIScale )))
          {
            _updater.SetIgnoredUpdate (SK_UTF8ToWideChar(_updater.GetResults().version));

            UpdatePromptPopup = PopupState_Closed;
            ImGui::CloseCurrentPopup ();
          }

          SKIF_ImGui_SetHoverTip ("SKIF will not prompt about this version again.");

          ImGui::SameLine ();
          ImGui::Spacing  ();
          ImGui::SameLine ();
        }

        SKIF_ImGui_DisallowMouseDragMove ( );

        if (ImGui::Button ("Cancel", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                               25 * SKIF_ImGui_GlobalDPIScale )))
        {
          UpdatePromptPopup = PopupState_Closed;
          ImGui::CloseCurrentPopup ();
        }

        SKIF_ImGui_DisallowMouseDragMove ( );

        ImGui::EndPopup ();
      }
      
      static float  HistoryPopupWidth          = 0.0f;
      static std::string HistoryPopupTitle;

      if (HistoryPopup == PopupState_Open && ! HiddenFramesContinueProcessing && ! SKIF_ImGui_IsAnyPopupOpen ( ))
      {
        HistoryPopupWidth = 360.0f;

        // 8.0f  per character
        // 25.0f for the scrollbar
        float calcHistoryPopupWidth = static_cast<float> (_updater.GetResults().history_formatted.max_length) * 8.0f + 25.0f;

        if (calcHistoryPopupWidth > HistoryPopupWidth)
          HistoryPopupWidth = calcHistoryPopupWidth;

        HistoryPopupWidth = std::min<float> (HistoryPopupWidth, SKIF_vecCurrentMode.x * 0.9f);

        HistoryPopupTitle = "Changelog";

        if (! _updater.GetChannel()->first.empty())
          HistoryPopupTitle += " (" + _updater.GetChannel()->first + ")";

        HistoryPopupTitle += "###History";

        ImGui::OpenPopup ("###History");
      
      }

      if (HistoryPopup == PopupState_Open ||
          HistoryPopup == PopupState_Opened)
      {
        ImGui::SetNextWindowSize (ImVec2 (HistoryPopupWidth * SKIF_ImGui_GlobalDPIScale, 0.0f));
        ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));
      }
      
      if (ImGui::BeginPopupModal (HistoryPopupTitle.c_str(), nullptr,
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_AlwaysAutoResize )
         )
      {
        if (HistoryPopup == PopupState_Open)
        {
          // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
          ImGuiWindow* window = ImGui::FindWindowByName ("###History");
          if (window != nullptr && ! window->Appearing)
            HistoryPopup = PopupState_Opened;
        }

        /*
        SKIF_ImGui_Spacing ();

        float fX = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(updateTxt.c_str()).x + ImGui::GetStyle().ItemSpacing.x) / 2;

        ImGui::SetCursorPosX(fX);

        ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success), updateTxt.c_str());
        */

        ImGui::Text        ("You are currently using");
        ImGui::SameLine    ( );
        ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), "Special K Image Viewer v " SKIV_VERSION_STR_A);

        SKIF_ImGui_Spacing ();

        if (! _updater.GetResults().history_formatted.notes.empty())
        {
          ImGui::PushStyleColor (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));
          ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));
          ImGui::PushFont       (fontConsolas);
          ImGui::InputTextEx    ( "###HistoryChanges", "No historical changes detected...",
                                    _updater.GetResults().history_formatted.notes.data(),
                                      static_cast<int>(_updater.GetResults().history_formatted.notes.size()),
                                        ImVec2 ( (HistoryPopupWidth - 15.0f) * SKIF_ImGui_GlobalDPIScale,
                                          std::min<float> (
                                              std::min<float>(_updater.GetResults().history_formatted.lines, 40.0f) * fontConsolas->FontSize,
                                              SKIF_vecCurrentMode.y * 0.6f)
                                               ), ImGuiInputTextFlags_ReadOnly | static_cast<ImGuiInputTextFlags_>(ImGuiInputTextFlags_Multiline));

          SKIF_ImGui_DisallowMouseDragMove ( );

          ImGui::PopFont        ( );
          ImGui::PopStyleColor  ( );
          ImGui::PopStyleColor  ( );

          SKIF_ImGui_Spacing ();
        }

        SKIF_ImGui_Spacing ();

        float fX = (ImGui::GetContentRegionAvail().x - 100 * SKIF_ImGui_GlobalDPIScale) / 2;

        ImGui::SetCursorPosX(fX);

        if (ImGui::Button ("Close", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                              25 * SKIF_ImGui_GlobalDPIScale )))
        {
          HistoryPopup = PopupState_Closed;
          ImGui::CloseCurrentPopup ();
        }

        SKIF_ImGui_DisallowMouseDragMove ( );

        ImGui::EndPopup ();
      }
      
      static float       AutoUpdatePopupWidth          = 0.0f;
      static std::string AutoUpdatePopupTitle;
      static bool        AutoUpdateChanges = (_updater.GetAutoUpdateNotes().max_length > 0 && false); //! _inject.SKVer32.empty() && _inject.SKVer32 == _registry.wsAutoUpdateVersion);

      if (AutoUpdateChanges)
      {
        AutoUpdateChanges = false;
        AutoUpdatePopup = PopupState_Open;
      }
      
      // Only open the popup prompt after the library has appeared (fixes the popup weirdly closing for some unknown reason)
      if (AutoUpdatePopup == PopupState_Open && ! HiddenFramesContinueProcessing && ! SKIF_ImGui_IsAnyPopupOpen ( ))
      {
        AutoUpdatePopupWidth = 360.0f;

        // 8.0f  per character
        // 25.0f for the scrollbar
        float calcAutoUpdatePopupWidth = static_cast<float> (_updater.GetAutoUpdateNotes().max_length) * 8.0f + 25.0f;

        if (calcAutoUpdatePopupWidth > AutoUpdatePopupWidth)
          AutoUpdatePopupWidth = calcAutoUpdatePopupWidth;

        AutoUpdatePopupWidth = std::min<float> (AutoUpdatePopupWidth, SKIF_vecCurrentMode.x * 0.9f);

        AutoUpdatePopupTitle = "An update was installed automatically###AutoUpdater";

        ImGui::OpenPopup ("###AutoUpdater");
      }

      if (AutoUpdatePopup == PopupState_Open ||
          AutoUpdatePopup == PopupState_Opened)
      {
        ImGui::SetNextWindowSize (ImVec2 (AutoUpdatePopupWidth* SKIF_ImGui_GlobalDPIScale, 0.0f));
        ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));
      }
      
      if (ImGui::BeginPopupModal (AutoUpdatePopupTitle.c_str(), nullptr,
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_AlwaysAutoResize )
         )
      {
        if (AutoUpdatePopup == PopupState_Open)
        {
          // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
          ImGuiWindow* window = ImGui::FindWindowByName ("###AutoUpdater");
          if (window != nullptr && ! window->Appearing)
            AutoUpdatePopup = PopupState_Opened;
        }

        /*
        SKIF_ImGui_Spacing ();

        float fX = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(updateTxt.c_str()).x + ImGui::GetStyle().ItemSpacing.x) / 2;

        ImGui::SetCursorPosX(fX);

        ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success), updateTxt.c_str());
        */

        ImGui::Text        ("You are now using");
        ImGui::SameLine    ( );
        ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), "Special K Image Viewer v " SKIV_VERSION_STR_A);

        SKIF_ImGui_Spacing ();

        if (! _updater.GetAutoUpdateNotes().notes.empty())
        {
          ImGui::PushStyleColor (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));
          ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));
          ImGui::PushFont       (fontConsolas);
          ImGui::InputTextEx    ( "###AutoUpdaterChanges", "No changes detected...",
                                    _updater.GetAutoUpdateNotes().notes.data(),
                                      static_cast<int>(_updater.GetAutoUpdateNotes().notes.size()),
                                        ImVec2 ( (AutoUpdatePopupWidth - 15.0f) * SKIF_ImGui_GlobalDPIScale,
                                          std::min<float> (
                                              std::min<float>(_updater.GetAutoUpdateNotes().lines, 40.0f) * fontConsolas->FontSize,
                                              SKIF_vecCurrentMode.y * 0.6f)
                                               ), ImGuiInputTextFlags_ReadOnly | static_cast<ImGuiInputTextFlags_>(ImGuiInputTextFlags_Multiline));

          SKIF_ImGui_DisallowMouseDragMove ( );

          ImGui::PopFont        ( );
          ImGui::PopStyleColor  ( );
          ImGui::PopStyleColor  ( );

          SKIF_ImGui_Spacing ();
        }

        SKIF_ImGui_Spacing ();

        float fX = (ImGui::GetContentRegionAvail().x - 100 * SKIF_ImGui_GlobalDPIScale) / 2;

        ImGui::SetCursorPosX(fX);

        if (ImGui::Button ("Close", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                              25 * SKIF_ImGui_GlobalDPIScale )))
        {
          _registry.regKVAutoUpdateVersion.putData(L"");
          AutoUpdatePopup = PopupState_Closed;
          ImGui::CloseCurrentPopup ();
        }

        SKIF_ImGui_DisallowMouseDragMove ( );

        ImGui::EndPopup ();
      }
      
      monitor_extent =
        ImGui::GetPopupAllowedExtentRect ( // ImGui::GetWindowAllowedExtentRect
          ImGui::GetCurrentWindowRead   ()
        );
      windowPos      = ImGui::GetWindowPos ();
      windowRect.Min = ImGui::GetWindowPos ();
      windowRect.Max = ImGui::GetWindowPos () + ImGui::GetWindowSize ();

      if (! HoverTipActive)
        HoverTipDuration = 0;

      // This allows us to ensure the window gets set within the workspace on the second frame after launch
      SK_RunOnce (
        RespectMonBoundaries = true
      );

      // This allows us to compact the working set on launch
#if 0
      SK_RunOnce (
        invalidatedFonts = SKIF_Util_timeGetTime ( )
      );

      if (invalidatedFonts > 0 &&
          invalidatedFonts + 500 < SKIF_Util_timeGetTime())
      {
        SKIF_Util_CompactWorkingSet ();
        invalidatedFonts = 0;
      }
#endif

      //OutputDebugString((L"Hidden frames: " + std::to_wstring(ImGui::GetCurrentWindow()->HiddenFramesCannotSkipItems) + L"\n").c_str());

#pragma region ClipboardReader

      // Read the clipboard
      // Optionally we'd filter out the data here too, but it would just
      //   duplicate the same processing we're already doing in viewer.cpp
      if (hotkeyCtrlV && ! ImGui::IsAnyItemActive ( )) // && ! ImGui::IsAnyItemFocused ( )
      {
        if (OpenClipboard (SKIF_ImGui_hWnd))
        {
          ClipboardData cbd = ClipboardData_None;

          // Enumerate the supported clipboard formats
          UINT format = 0;
          while ((format = EnumClipboardFormats (format)) != 0)
          {
            cbd |= ((format == CF_TEXT       ) ? ClipboardData_TextANSI    :
                    (format == CF_UNICODETEXT) ? ClipboardData_TextUnicode :
                    (format == CF_HTML       ) ? ClipboardData_HTML        :
                    (format == CF_BITMAP     ) ? ClipboardData_Bitmap      :
                    (format == CF_HDROP      ) ? ClipboardData_HDROP       :
                                                 ClipboardData_None       );
//#ifdef _DEBUG
            wchar_t wzFormatName[256];
            GetClipboardFormatNameW (format, wzFormatName, 256);
            PLOG_VERBOSE << "Supported clipboard format: " << format << " - " << std::wstring(wzFormatName);
//#endif
          }

          if (cbd & ClipboardData_TextUnicode)
          {
            PLOG_VERBOSE << "Received a CF_UNICODETEXT paste!";

            std::wstring unicode = SKIF_Util_GetClipboardTextDataW ( );

            if (! unicode.empty())
            {
              dragDroppedFilePath = unicode;

              ImGui::InsertNotification({ ImGuiToastType::Info, 3000, "Received a Unicode paste", "%s", SK_WideCharToUTF8 (dragDroppedFilePath).c_str()});
            }
          }

          else if (cbd & ClipboardData_TextANSI)
          {
            PLOG_VERBOSE << "Received a CF_TEXT paste!";

            std::string ansi = SKIF_Util_GetClipboardTextData ( );

            if (! ansi.empty())
            {
              dragDroppedFilePath = SK_UTF8ToWideChar (ansi);

              ImGui::InsertNotification({ ImGuiToastType::Info, 3000, "Received a ANSI paste", "%s", SK_WideCharToUTF8 (dragDroppedFilePath).c_str()});
            }
          }

          else if (cbd & ClipboardData_HTML)
          {
            PLOG_VERBOSE << "Received a CF_HTML paste!";

            std::string html      = SKIF_Util_GetClipboardHTMLData ( );
            std::string htmlLower = SKIF_Util_ToLower (html);
            std::string split1    = R"(<img )"; // Split first at '<img '
            std::string split2    = R"(src=")"; // Split next  at 'src="'
            std::string split3    = R"(")";     // Split last  at '"'

            // Split 1 (trim before IMG element)
            if (htmlLower.find (split1) != std::wstring::npos)
            {
              html = html.substr (htmlLower.find (split1) + split1.length());

#ifdef _DEBUG
              PLOG_VERBOSE << "html: " << html;
#endif

              // Update lower (trim before SRC attribute)
              htmlLower = SKIF_Util_ToLower (html);

              // Split 2
              if (htmlLower.find (split2) != std::wstring::npos)
              {
                html = html.substr (htmlLower.find (split2) + split2.length());

#ifdef _DEBUG
                PLOG_VERBOSE << "html: " << html;
#endif

                // Update lower
                htmlLower = SKIF_Util_ToLower (html);

                // Split 3 (trim everything after SRC value)
                if (htmlLower.find (split3) != std::wstring::npos)
                {
                  html = html.substr (0, htmlLower.find (split3));

#ifdef _DEBUG
                  PLOG_VERBOSE << "html: " << html;
#endif

                  if (! html.empty())
                  {
                    fb::HtmlCoder html_decoder;
                    html_decoder.decode(html);

                    PLOG_VERBOSE << "Extracted image URL path: " << html;
                    dragDroppedFilePath = SK_UTF8ToWideChar (html);

                    ImGui::InsertNotification({ ImGuiToastType::Info, 3000, "Received a HTML paste", "%s", SK_WideCharToUTF8(dragDroppedFilePath).c_str()});
                  }
                }
              }
            }
          }

          else if (cbd & ClipboardData_Bitmap)
          {
            PLOG_VERBOSE << "Received a CF_BITMAP paste!";

            SKIF_Util_GetClipboardBitmapData ( );

            ImGui::InsertNotification({ ImGuiToastType::Info, 3000, "Received a bitmap paste" });
          }

          else if ((cbd & ClipboardData_HDROP))
          {
            PLOG_VERBOSE << "Detected a drop of type CF_HDROP";

            std::wstring unicode = SKIF_Util_GetClipboardHDROP ( );

            if (! unicode.empty())
            {
              dragDroppedFilePath = unicode;

              ImGui::InsertNotification({ ImGuiToastType::Info, 3000, "Received a file path drop", "%s", SK_WideCharToUTF8 (dragDroppedFilePath).c_str()});
            }
          }

          CloseClipboard ( );
        }
      }

#pragma endregion

      // Main rendering function
      ImGui::RenderNotifications ( );

      // End the main ImGui window
      ImGui::End ( );
    }

#pragma endregion

    // Do stuff when focus is changed
    static int
        AppHasFocus  = -1;
    if (_registry.bControllers &&
        AppHasFocus != (int)SKIF_ImGui_IsFocused())
    {   AppHasFocus  = (int)SKIF_ImGui_IsFocused();

      // If focus was received
      if (AppHasFocus)
      {
        PLOG_VERBOSE << "Waking...";
        _gamepad.WakeThread  ( );
      }

      // If focus was lost
      else
      {
        PLOG_VERBOSE << "Sleeping...";
        _gamepad.SleepThread ( );
      }
    }

    // If there is any popups opened when SKIF is unfocused and not hovered, close them.
    // This can probably mistakenly bug out, seeing how the focus state isn't tracked reliable at times
    if (! SKIF_ImGui_IsFocused ( ) && ! ImGui::IsAnyItemHovered ( ) && SKIF_ImGui_IsAnyPopupOpen ( ))
    {
      // But don't close those of interest
      if ( PopupMessageInfo != PopupState_Open   &&
           PopupMessageInfo != PopupState_Opened &&
          UpdatePromptPopup != PopupState_Open   &&
          UpdatePromptPopup != PopupState_Opened &&
               HistoryPopup != PopupState_Open   &&
               HistoryPopup != PopupState_Opened &&
            AutoUpdatePopup != PopupState_Open   &&
            AutoUpdatePopup != PopupState_Opened)
        ImGui::ClosePopupsOverWindow (ImGui::GetCurrentWindowRead ( ), false);
    }

    // Actual rendering is conditional, this just processes input and ends the ImGui frame.
    ImGui::Render (); // also calls ImGui::EndFrame ();

    if (SKIF_ImGui_hWnd != NULL)
    {
      SK_RunOnce (SKIF_Shell_CreateJumpList ( ));
    }

    // Conditional rendering, but only if SKIF_ImGui_hWnd has actually been created
    bool bRefresh = (SKIF_ImGui_hWnd != NULL && IsIconic (SKIF_ImGui_hWnd)) ? false : true;

    if (invalidatedDevice > 0 && SKIF_Tab_Selected == UITab_Viewer)
      bRefresh = false;

    // Disable navigation highlight on first frames
    SK_RunOnce(
      ImGuiContext& g = *ImGui::GetCurrentContext();
      g.NavDisableHighlight = true;
    );

    // From ImHex: https://github.com/WerWolv/ImHex/blob/09bffb674505fa2b09f0135a519d213f6fb6077e/main/gui/source/window/window.cpp#L631-L672
    // GPL-2.0 license: https://github.com/WerWolv/ImHex/blob/master/LICENSE
    if (bRefresh)
    {
      bRefresh = false;
      static std::vector<uint8_t> previousVtxData;
      static size_t previousVtxDataSize = 0;
      size_t offset = 0;
      size_t vtxDataSize = 0;

      for (const auto viewPort : ImGui::GetPlatformIO().Viewports) {
        auto drawData = viewPort->DrawData;
        for (int n = 0; n < drawData->CmdListsCount; n++) {
          vtxDataSize += drawData->CmdLists[n]->VtxBuffer.size() * sizeof(ImDrawVert);
        }
      }
      for (const auto viewPort : ImGui::GetPlatformIO().Viewports) {
        auto drawData = viewPort->DrawData;
        for (int n = 0; n < drawData->CmdListsCount; n++) {
          const ImDrawList *cmdList = drawData->CmdLists[n];

          if (vtxDataSize == previousVtxDataSize) {
            bRefresh = bRefresh || std::memcmp(previousVtxData.data() + offset, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.size() * sizeof(ImDrawVert)) != 0;
          } else {
            bRefresh = true;
          }

          if (previousVtxData.size() < offset + cmdList->VtxBuffer.size() * sizeof(ImDrawVert)) {
            previousVtxData.resize(offset + cmdList->VtxBuffer.size() * sizeof(ImDrawVert));
          }

          std::memcpy(previousVtxData.data() + offset, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.size() * sizeof(ImDrawVert));
          offset += cmdList->VtxBuffer.size() * sizeof(ImDrawVert);
        }
      }

      previousVtxDataSize = vtxDataSize;
    }

    // Update, Render and Present the main and any additional Platform Windows
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
      // This recreates any additional viewports (index 1+)
      if (RecreateWin32Windows)
      {   RecreateWin32Windows = false;

        // If the Win32 windows should be recreated, we set the LastFrameActive to 0 here to
        //   force ImGui::UpdatePlatformWindows() below to recreate them.
        for (int i = 1; i < ImGui::GetCurrentContext()->Viewports.Size; i++)
        {
          ImGuiViewportP* viewport = ImGui::GetCurrentContext()->Viewports[i];
          viewport->LastFrameActive = 0;
        }
      }
    }

    ImGui::UpdatePlatformWindows ( ); // This creates all ImGui related windows, including the main application window, and also updates the window and swapchain sizes etc

    // Update the title of the main app window to indicate we're loading...
    // This is a fix for the window title being set wrong on launch when we started loading an image
    //   before the Win32 window was even created
    static bool fixLoadingWindowTitleOnLaunch = true;
    if (fixLoadingWindowTitleOnLaunch && SKIF_ImGui_hWnd != NULL)
    {
      fixLoadingWindowTitleOnLaunch = false;

      extern bool tryingToLoadImage;
      extern bool tryingToDownImage;
      if (tryingToDownImage)
        ::SetWindowText (SKIF_ImGui_hWnd, L"Downloading... - " SKIV_WINDOW_TITLE_SHORT_W);
      else if (tryingToLoadImage)
        ::SetWindowText (SKIF_ImGui_hWnd, L"Loading... - " SKIV_WINDOW_TITLE_SHORT_W);
    }

    if (bRefresh)
    {
      // This renders the main viewport (index 0)
      ImGui_ImplDX11_RenderDrawData (ImGui::GetDrawData ());

      // This renders any additional viewports (index 1+)
      ImGui::RenderPlatformWindowsDefault (); // Also eventually calls ImGui_ImplDX11_SwapBuffers ( ) which Presents ( )

      // Ensure we also have a dragdrop target on the main window
      if (SKIF_ImGui_hWnd != NULL)
        _drag_drop.Register (SKIF_ImGui_hWnd);

      // This runs only once, after the ImGui window has been created
      static bool
          runOnce = true;
      if (runOnce && SKIF_ImGui_hWnd != NULL)
      {   runOnce = false;

        SKIF_Util_GetMonitorHzPeriod (SKIF_ImGui_hWnd, MONITOR_DEFAULTTOPRIMARY, dwDwmPeriod);
        //OutputDebugString((L"Initial refresh rate period: " + std::to_wstring (dwDwmPeriod) + L"\n").c_str());

        // Spawn the gamepad input thread
        if (! _Signal.Launcher && ! _Signal.LauncherURI && ! _Signal.Quit && ! _Signal.ServiceMode)
          _gamepad.SpawnChildThread         ( );
      }
    }

    if ( startedMinimized && SKIF_ImGui_IsFocused ( ) )
    {
      startedMinimized = false;
      if ( _registry.bOpenAtCursorPosition )
        RepositionSKIF = true;
    }

    // Release any leftover resources from last frame
    IUnknown* pResource = nullptr;
    while (! SKIF_ResourcesToFree.empty ())
    {
      if (SKIF_ResourcesToFree.try_pop (pResource))
      {
        CComPtr <IUnknown> ptr = pResource;
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Releasing " << ptr.p;
        ptr.p->Release();
      }
      
      if (invalidatedDevice == 2)
        invalidatedDevice = 0;
    }

    // If process should stop, post WM_QUIT
    if ((! bKeepProcessAlive))// && SKIF_ImGui_hWnd != 0)
      PostQuitMessage (0);
      //PostMessage (hWnd, WM_QUIT, 0x0, 0x0);

    // Handle dynamic pausing
    bool pause = false;
    static int
      processAdditionalFrames = 0;

    bool input = SKIF_ImGui_IsAnyInputDown ( ) || uiLastMsg == WM_SKIF_GAMEPAD ||
                   (uiLastMsg >= WM_MOUSEFIRST && uiLastMsg <= WM_MOUSELAST)   ||
                   (uiLastMsg >= WM_KEYFIRST   && uiLastMsg <= WM_KEYLAST  );
    
    // We want SKIF to continue rendering in some specific scenarios
    ImGuiWindow* wnd = ImGui::FindWindowByName ("###KeyboardHint");
    if (wnd != nullptr && wnd->Active)
      processAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If the keyboard hint/search is active
    else if (uiLastMsg == WM_SETCURSOR  || uiLastMsg == WM_TIMER   ||
             uiLastMsg == WM_SETFOCUS   || uiLastMsg == WM_KILLFOCUS)
      processAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If we received some event changes
    else if (input)
      processAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If we received any gamepad input or an input is held down
    else if (svcTransitionFromPendingState)
      processAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If we transitioned away from a pending service state
    else if (1.0f > ImGui::GetCurrentContext()->DimBgRatio && ImGui::GetCurrentContext()->DimBgRatio > 0.0f)
      processAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If the background is currently currently undergoing a fade effect
    else if (SKIF_Tab_Selected == UITab_Viewer && imageFadeActive)
      processAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If the cover is currently undergoing a fade effect
    else if (ImGui::notifications.size() > 0)
      processAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If we have any visible notifications
    else if (addAdditionalFrames > 0)
      processAdditionalFrames = ImGui::GetFrameCount ( ) + addAdditionalFrames; // Used when the cover is currently loading in, or the update check just completed
    /*
    else if (  AddGamePopup == PopupState_Open ||
               ConfirmPopup == PopupState_Open ||
            ModifyGamePopup == PopupState_Open ||
          UpdatePromptPopup == PopupState_Open ||
               HistoryPopup == PopupState_Open )
      processAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If a popup is transitioning to an opened state
    */
    else if (ImGui::GetFrameCount ( ) > processAdditionalFrames)
      processAdditionalFrames = 0;

    addAdditionalFrames = 0;

    //OutputDebugString((L"Framerate: " + std::to_wstring(ImGui::GetIO().Framerate) + L"\n").c_str());

    // Clear gamepad/nav input for the next frame as we're done with it
    //memset (ImGui::GetIO ( ).NavInputs, 0, sizeof(ImGui::GetIO ( ).NavInputs));

    //if (uiLastMsg == WM_SKIF_GAMEPAD)
    //  OutputDebugString(L"[doWhile] Message spotted: WM_SKIF_GAMEPAD\n");
    //else if (uiLastMsg == WM_SKIF_COVER)
    //  OutputDebugString(L"[doWhile] Message spotted: WM_SKIF_COVER\n");
    //else if (uiLastMsg != 0x0)
    //  OutputDebugString((L"[doWhile] Message spotted: " + std::to_wstring(uiLastMsg) + L"\n").c_str());
    
    // Pause if we don't need to render any additional frames
    if (processAdditionalFrames == 0)
      pause = true;

    // Don't pause if there's hidden frames that needs rendering
    if (HiddenFramesContinueProcessing)
      pause = false;

    // Follow up on our attempt to restart the Steam client
    if (SteamProcessHandle != NULL &&
        SteamProcessHandle != INVALID_HANDLE_VALUE)
    {
      // When Steam has closed, restart it again
      if (WaitForSingleObject (SteamProcessHandle, 0) != WAIT_TIMEOUT)
      {
        // Stop waiting for it on all tabs
        for (auto& vWatchHandle : vWatchHandles)
        {
          if (! vWatchHandle.empty())
            vWatchHandle.erase(std::remove(vWatchHandle.begin(), vWatchHandle.end(), SteamProcessHandle), vWatchHandle.end());
        }

        PLOG_INFO << "Starting up the Steam client...";
        SKIF_Util_OpenURI (L"steam://open/main");

        CloseHandle (SteamProcessHandle);
        SteamProcessHandle = NULL;
      }
    }

    SK_RunOnce (PLOG_INFO << "Processed first frame! Start -> End took " << (SKIF_Util_timeGetTime1() - SKIF_firstFrameTime) << " ms.");

    do
    {
      // Pause rendering
      if (pause)
      {
        static bool bWaitTimeoutMsgInputFallback = false;

        // Empty working set before we pause
        // - Bad idea because it will immediately hitch when that stuff has to be moved back from the pagefile
        //   There's no predicting how long it will take to move those pages back into memory
        SK_RunOnce (SKIF_Util_CompactWorkingSet ( ));

        // Create/update the timer when we are pausing
        if (_registry.bEfficiencyMode && ! _registry._EfficiencyMode && SKIF_Notify_hWnd != NULL && ! msgDontRedraw && ! SKIF_ImGui_IsFocused ( ))
          SetTimer (SKIF_Notify_hWnd, cIDT_TIMER_EFFICIENCY, 1000, (TIMERPROC) &SKIF_EfficiencyModeTimerProc);

        // Sleep until a message is in the queue or a change notification occurs
        DWORD res =
          MsgWaitForMultipleObjects (static_cast<DWORD>(vWatchHandles[SKIF_Tab_Selected].size()), vWatchHandles[SKIF_Tab_Selected].data(), false, bWaitTimeoutMsgInputFallback ? dwDwmPeriod : INFINITE, QS_ALLINPUT);

        // The below is required as a fallback if V-Sync OFF is forced on SKIF and e.g. analog stick drift is causing constant input.
        // Throttle to monitors refresh rate unless a new event is triggered, or user input is posted, but only if the frame rate is detected as being unlocked
        if (res == WAIT_FAILED)
        {
          SK_RunOnce (
          {
            PLOG_ERROR << "Waiting on a new message or change notification failed with error message: " << SKIF_Util_GetErrorAsWStr ( );
            PLOG_ERROR << "Timeout has permanently been set to the monitors refresh rate period (" << dwDwmPeriod << ") !";
            bWaitTimeoutMsgInputFallback = true;
          });
        }

        // Always render 3 additional frames after we wake up
        processAdditionalFrames = ImGui::GetFrameCount() + 3;
      }

      if (bRefresh && ! msgDontRedraw && SKIF_ImGui_hWnd != NULL && ! vSwapchainWaitHandles.empty())
      {
        static bool frameRateUnlocked = false;
        static int  unlockedCount     = 0;

        // If the frame rate was ever detected as being unlocked, use sleep as a limiter instead
        if (frameRateUnlocked)
          Sleep (dwDwmPeriod);

        else
        {
          static bool bWaitTimeoutSwapChainsFallback = false;
          //auto timePre = SKIF_Util_timeGetTime1 ( );

          DWORD res =
            WaitForMultipleObjectsEx (static_cast<DWORD>(vSwapchainWaitHandles.size()), vSwapchainWaitHandles.data(), true, bWaitTimeoutSwapChainsFallback ? dwDwmPeriod : 1000, true);

          //OutputDebugString((L"[" + SKIF_Util_timeGetTimeAsWStr() + L"][#" + std::to_wstring(ImGui::GetFrameCount()) + L"] Maybe we'll be waiting? (handles: " + std::to_wstring(vSwapchainWaitHandles.size()) + L")\n").c_str());
          if (res == WAIT_TIMEOUT)
          {
            // This is only expected to occur when an issue arises
            // e.g. the display driver resets and invalidates the
            // swapchain in the middle of a frame.
            PLOG_ERROR << "Timed out while waiting on the swapchain wait objects!";
          }

          // Only reason we use a timeout here is in case a swapchain gets destroyed on the same frame we try waiting on its handle
          else if (res == WAIT_FAILED)
          {
            SK_RunOnce (
            {
              PLOG_ERROR << "Waiting on the swapchain wait objects failed with error message: " << SKIF_Util_GetErrorAsWStr ( );
              PLOG_ERROR << "Timeout has permanently been set to the monitors refresh rate period (" << dwDwmPeriod << ") !";
              bWaitTimeoutSwapChainsFallback = true;
            });
          }

#if 0
          auto timePost = SKIF_Util_timeGetTime1 ( );
          auto timeDiff = timePost - timePre;

          if (! frameRateUnlocked && timeDiff <= 4 && ImGui::GetFrameCount ( ) > 240 && static_cast<DWORD>(ImGui::GetIO().Framerate) > (1000 / (dwDwmPeriod)))
            unlockedCount++;

          if (unlockedCount > 10)
            frameRateUnlocked = true;

          //PLOG_VERBOSE << "Waited: " << timeDiff << " ms (handles : " << vSwapchainWaitHandles.size() << ")";
#endif
        }
      }
      
      // Reset stuff that's set as part of pumping the message queue
      msgDontRedraw = false;
      uiLastMsg     = 0x0;

      // Pump the message queue, and break if we receive a false (WM_QUIT or WM_QUERYENDSESSION)
      if (! _TranslateAndDispatch ( ))
        break;
      
      // If we added more frames, ensure we exit the loop
      if (addAdditionalFrames > 0)
        msgDontRedraw = false;

      // Disable Efficiency Mode when we are being interacted with
      if (_registry._EfficiencyMode && SKIF_Notify_hWnd != NULL && ! msgDontRedraw && SKIF_ImGui_IsFocused ( ))
      {
        // Wake up and disable idle priority + ECO QoS (let the system take over)
        SetPriorityClass (SKIF_Util_GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
        SKIF_Util_SetProcessPowerThrottling (SKIF_Util_GetCurrentProcess(), -1);

        PLOG_DEBUG << "Disengaged efficiency mode";

        _registry._EfficiencyMode = false;
      }

      // Break if SKIF is no longer a window
      //if (! IsWindow (hWnd))
      //  break;

    } while (! SKIF_Shutdown.load() && msgDontRedraw); // For messages we don't want to redraw on, we set msgDontRedraw to true.
  }

  PLOG_INFO << "Exited main loop...";

  PLOG_INFO << "Killing timers...";
  KillTimer (SKIF_Notify_hWnd, IDT_REFRESH_TOOLTIP);
  KillTimer (SKIF_Notify_hWnd, IDT_REFRESH_UPDATER);
  KillTimer (SKIF_Notify_hWnd, IDT_REFRESH_GAMES);
  KillTimer (SKIF_Notify_hWnd, cIDT_TIMER_EFFICIENCY);

  PLOG_INFO << "Disabling the dragdrop target...";
  _drag_drop.Revoke (SKIF_ImGui_hWnd);

  PLOG_INFO << "Shutting down ImGui...";
  ImGui_ImplDX11_Shutdown     ( );
  ImGui_ImplWin32_Shutdown    ( );

  CleanupDeviceD3D            ( );

  PLOG_INFO << "Destroying notification icon...";
  _gamepad.UnregisterDevNotification ( );
  SKIF_Shell_DeleteNotifyIcon ( );
  DestroyWindow             (SKIF_Notify_hWnd);

  PLOG_INFO << "Destroying ImGui context...";
  ImGui::DestroyContext       ( );

  SKIF_ImGui_hWnd  = NULL;
  SKIF_Notify_hWnd = NULL;

  DeleteCriticalSection (&CriticalSectionDbgHelp);

  PLOG_INFO << "Exiting process with code " << SKIF_ExitCode;
  return SKIF_ExitCode;
}


// Helper functions

// D3D9 test stuff
//#define SKIF_D3D9_TEST

#ifdef SKIF_D3D9_TEST

#define D3D_DEBUG_INFO
#pragma comment (lib, "d3d9.lib")
#include <D3D9.h>

#endif

bool CreateDeviceD3D (HWND hWnd)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

#ifdef SKIF_D3D9_TEST
  /* Test D3D9 debugging */
  IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);

  if (d3d != nullptr)
  {
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = 800;
    pp.BackBufferHeight = 600;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.MultiSampleType = D3DMULTISAMPLE_NONE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = NULL;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D16;

    IDirect3DDevice9* device = nullptr;
    // Intentionally passing an invalid parameter to CreateDevice to cause an exception to be thrown by the D3D9 debug layer
    //HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, (D3DDEVTYPE)100, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);

    if (FAILED(hr))
    {
      //OutputDebugString(L"d3d->CreateDevice() failed!\n");
    }
  }

  else {
    OutputDebugString(L"Direct3DCreate9() failed!\n");
  }
#endif
  
  CComPtr <IDXGIFactory2> pFactory2;

  if (FAILED (CreateDXGIFactory1 (__uuidof (IDXGIFactory2), (void **)&pFactory2.p)))
    return false;

  // Windows 7 (2013 Platform Update), or Windows 8+
  // SKIF_bCanFlip            =         true; // Should never be set to false here

  // Windows 8.1+
  _registry._RendererCanWaitSwapchain      =
    SKIF_Util_IsWindows8Point1OrGreater ();

  // Windows 10 1709+ (Build 16299)
  _registry._RendererCanHDR                =
    SKIF_Util_IsWindows10v1709OrGreater (    ) &&
    SKIF_Util_IsHDRActive               (true);

  CComQIPtr <IDXGIFactory5>
                  pFactory5 (pFactory2.p);

  // Windows 10+
  if (pFactory5 != nullptr)
  {
    BOOL supportsTearing = FALSE;
    pFactory5->CheckFeatureSupport (
                          DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                        &supportsTearing,
                                sizeof  (supportsTearing)
                                              );
    _registry._RendererCanAllowTearing = (supportsTearing != FALSE);

    pFactory5.Release();
  }

  // Overrides
  //_registry._RendererCanAllowTearing       = false; // Allow Tearing
  //SKIF_bCanFlip               = false; // Flip Sequential (if this is false, BitBlt Discard will be used instead)
  //SKIF_bCanWaitSwapchain      = false; // Waitable Swapchain

  // D3D11Device
  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL
                    featureLevelArray [4] = {
    D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
  };

  UINT createDeviceFlags = 0;
  // This MUST be disabled before public release! Otherwise systems without the Windows SDK installed will crash on launch.
  //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG; // Enable debug layer of D3D11

  if (FAILED (D3D11CreateDevice ( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                              createDeviceFlags, featureLevelArray,
                                                         sizeof (featureLevelArray) / sizeof featureLevel,
                                                D3D11_SDK_VERSION,
                                                       &SKIF_pd3dDevice,
                                                                &featureLevel,
                                                       &SKIF_pd3dDeviceContext)))
  {
    //OutputDebugString(L"D3D11CreateDevice failed!\n");
    PLOG_ERROR << "D3D11CreateDevice failed!";
    return false;
  }

  //return true; // No idea why this was left in https://github.com/SpecialKO/SKIF/commit/1c03d60642fcc62d4aa27bd440dc24115f6cf907 ... A typo probably?

  // We need to try creating a dummy swapchain before we actually start creating
  //   viewport windows. This is to ensure a compatible format is used from the
  //   get go, as e.g. using WS_EX_NOREDIRECTIONBITMAP on a BitBlt window will
  //   cause it to be hidden entirely.

  if (pFactory2 != nullptr)
  {
    CComQIPtr <IDXGISwapChain1>
                   pSwapChain1;

    DXGI_FORMAT dxgi_format;

    // HDR formats
    if (_registry._RendererCanHDR && _registry.iHDRMode > 0)
    {
#ifdef SKIV_HDR10_SUPPORT
      if      (_registry.iHDRMode == 2)
        dxgi_format = DXGI_FORMAT_R16G16B16A16_FLOAT; // scRGB (16 bpc)
      else
        dxgi_format = DXGI_FORMAT_R10G10B10A2_UNORM;  // HDR10 (10 bpc)
#else
      dxgi_format = DXGI_FORMAT_R16G16B16A16_FLOAT; // scRGB (16 bpc)
#endif
    }

    // SDR formats
    else {
      if      (_registry.iSDRMode == 2)
        dxgi_format = DXGI_FORMAT_R16G16B16A16_FLOAT; // 16 bpc
      else if (_registry.iSDRMode == 1 && SKIF_Util_IsWindowsVersionOrGreater (10, 0, 16299))
        dxgi_format = DXGI_FORMAT_R10G10B10A2_UNORM;  // 10 bpc (apparently only supported for flip on Win10 1709+
      else
        dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM;     // 8 bpc;
    }

    // Create a dummy swapchain for the dummy viewport
    DXGI_SWAP_CHAIN_DESC1
      swap_desc                  = { };
    swap_desc.Width              = 8;
    swap_desc.Height             = 8;
    swap_desc.Format             = dxgi_format;
    swap_desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.Flags              = 0x0;
    swap_desc.SampleDesc.Count   = 1;
    swap_desc.SampleDesc.Quality = 0;

    // Assume flip by default
    swap_desc.BufferCount  = 3; // Must be 2-16 for flip model

    if (_registry._RendererCanWaitSwapchain)
      swap_desc.Flags     |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    if (_registry._RendererCanAllowTearing)
      swap_desc.Flags     |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    for (auto  _swapEffect : {DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, DXGI_SWAP_EFFECT_DISCARD}) // DXGI_SWAP_EFFECT_FLIP_DISCARD
    {
      swap_desc.SwapEffect = _swapEffect;

      // In case flip failed, fall back to using BitBlt
      if (_swapEffect == DXGI_SWAP_EFFECT_DISCARD)
      {
        swap_desc.Format       = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_desc.BufferCount  = 1;
        swap_desc.Flags        = 0x0;
        _registry._RendererCanHDR           = false;
        _registry._RendererCanWaitSwapchain = false;
        _registry._RendererCanAllowTearing = false;
        _registry.iUIMode      = 0;
      }

      if (SUCCEEDED (pFactory2->CreateSwapChainForHwnd (SKIF_pd3dDevice, hWnd, &swap_desc, NULL, NULL,
                                &pSwapChain1 )))
      {
        pSwapChain1.Release();
        break;
      }
    }

    pFactory2.Release();

    return true;
  }

  //CreateRenderTarget ();

  return false;
}

void CleanupDeviceD3D (void)
{
  //CleanupRenderTarget ();

  //IUnknown_AtomicRelease ((void **)&g_pSwapChain);
  IUnknown_AtomicRelease ((void **)&SKIF_pd3dDeviceContext);
  IUnknown_AtomicRelease ((void **)&SKIF_pd3dDevice);
}

// Prevent race conditions between asset loading and device init
//
void SKIF_WaitForDeviceInitD3D (void)
{
  while (SKIF_pd3dDevice        == nullptr    ||
         SKIF_pd3dDeviceContext == nullptr /* ||
         SKIF_g_pSwapChain        == nullptr  */ )
  {
    Sleep (10UL);
  }
}

CComPtr <ID3D11Device>
SKIF_D3D11_GetDevice (bool bWait)
{
  if (bWait)
    SKIF_WaitForDeviceInitD3D ();

  return
    SKIF_pd3dDevice;
}

bool SKIF_D3D11_IsDevicePtr (void)
{
  return (SKIF_pd3dDevice != nullptr)
                     ? true : false;
}

/*
void CreateRenderTarget (void)
{
  ID3D11Texture2D*                           pBackBuffer = nullptr;
  g_pSwapChain->GetBuffer (0, IID_PPV_ARGS (&pBackBuffer));

  if (pBackBuffer != nullptr)
  {
    g_pd3dDevice->CreateRenderTargetView   ( pBackBuffer, nullptr, &SKIF_g_mainRenderTargetView);
                                             pBackBuffer->Release ();
  }
}

void CleanupRenderTarget (void)
{
  IUnknown_AtomicRelease ((void **)&SKIF_g_mainRenderTargetView);
}
*/

// Win32 message handler
LRESULT
WINAPI
SKIF_WndProc (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  UNREFERENCED_PARAMETER (hWnd);
  UNREFERENCED_PARAMETER (lParam);

  // This is the message procedure that handles all custom SKIF window messages and actions
  
  UpdateFlags uFlags = UpdateFlags_Unknown;
  
  static SKIF_CommonPathsCache&   _path_cache = SKIF_CommonPathsCache  ::GetInstance ( );
  static SKIF_RegistrySettings&   _registry   = SKIF_RegistrySettings  ::GetInstance ( );
  static SKIF_GamePadInputHelper& _gamepad    = SKIF_GamePadInputHelper::GetInstance ( );
  // We don't define this here to ensure it doesn't get created before we are ready to handle it
//static SKIF_Updater&          _updater    = SKIF_Updater         ::GetInstance ( );

  switch (msg)
  {
    case WM_COPYDATA:
    {
      PCOPYDATASTRUCT data = (PCOPYDATASTRUCT) lParam;

      if (data->dwData == SKIV_CDS_STRING &&
          data->lpData != nullptr)
      {
        wchar_t                    wszFilePath [MAX_PATH] = { };
        if (S_OK == StringCbCopyW (wszFilePath, MAX_PATH, (LPCWSTR)data->lpData))
        {
          extern std::wstring dragDroppedFilePath;
          dragDroppedFilePath = std::wstring(wszFilePath);

          PLOG_VERBOSE << "WM_COPYDATA: " << dragDroppedFilePath;

          // Restore, and whatnot
          if (SKIF_ImGui_hWnd != NULL)
          {
            if (IsIconic (SKIF_ImGui_hWnd))
              ShowWindow (SKIF_ImGui_hWnd, SW_RESTORE);

            if (! UpdateWindow        (SKIF_ImGui_hWnd))
              PLOG_DEBUG << "UpdateWindow ( ) failed!";

            if (! SetForegroundWindow (SKIF_ImGui_hWnd))
              PLOG_DEBUG << "SetForegroundWindow ( ) failed!";

            if (! SetActiveWindow     (SKIF_ImGui_hWnd))
              PLOG_DEBUG << "SetActiveWindow ( ) failed: "  << SKIF_Util_GetErrorAsWStr ( );

            if (! BringWindowToTop    (SKIF_ImGui_hWnd))
              PLOG_DEBUG << "BringWindowToTop ( ) failed: " << SKIF_Util_GetErrorAsWStr ( );
          }

          extern bool tryingToDownImage;
          tryingToDownImage = false;

          return true; // Signal the other instance that we successfully handled the data
        }
      }

      return false;
    }

    case WM_DEVICECHANGE:
      switch (wParam)
      {
        case DBT_DEVICEARRIVAL:
        case DBT_DEVICEREMOVECOMPLETE:
        {
          DEV_BROADCAST_HDR* pDevHdr =
            (DEV_BROADCAST_HDR *)lParam;
          if (pDevHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
          {
            DEV_BROADCAST_DEVICEINTERFACE_W *pDev =
              (DEV_BROADCAST_DEVICEINTERFACE_W *)pDevHdr;

            static constexpr GUID GUID_DEVINTERFACE_HID =
              { 0x4D1E55B2L, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

            if (IsEqualGUID (pDev->dbcc_classguid, GUID_DEVINTERFACE_HID))
            {
              // Check for changes in case any device has arrived
              if (wParam == DBT_DEVICEARRIVAL)
              {
                PLOG_VERBOSE << "A HID device has arrived, and we need to refresh gamepad connectivity...";
                _gamepad.InvalidateGamePads( );
                _gamepad.WakeThread ( );
              }

              // Only check for changes if a device was removed if we actually had a connected gamepad
              else if (wParam == DBT_DEVICEREMOVECOMPLETE && _gamepad.HasGamePad())
              {
                PLOG_VERBOSE << "A HID device was removed, and we need to refresh gamepad connectivity...";
                _gamepad.InvalidateGamePads( );
                _gamepad.WakeThread ( );
              }
            }
          }
        }
      }
      break;

    case WM_HOTKEY:
      if (wParam == SKIF_HotKey_HDR)
        SKIF_Util_EnableHDROutput ( );
      if (wParam == SKIV_HotKey_Snip)
      {
        extern HRESULT
        SKIV_Image_CaptureDesktop (DirectX::ScratchImage& image, int flags = 0x0);

        DirectX::ScratchImage                     captured_img;
        if (SUCCEEDED (SKIV_Image_CaptureDesktop (captured_img)))
        {
          extern bool
          SKIV_HDR_ConvertImageToPNG (const DirectX::Image& raw_hdr_img, DirectX::ScratchImage& png_img);
          extern bool
          SKIV_HDR_SavePNGToDisk (const wchar_t* wszPNGPath, const DirectX::Image* png_image,
                                                             const DirectX::Image* raw_image,
                                     const char* szUtf8MetadataTitle);
          extern bool
          SKIV_PNG_CopyToClipboard (const DirectX::Image& image, const void *pData, size_t data_size);

          DirectX::ScratchImage                                       hdr_img;
          if (SKIV_HDR_ConvertImageToPNG (*captured_img.GetImages (), hdr_img))
          {
            wchar_t                         wszPNGPath [MAX_PATH + 2] = { };
            GetCurrentDirectoryW (MAX_PATH, wszPNGPath);

            PathAppendW       (wszPNGPath, L"SKIV_HDR_Clipboard");
            PathAddExtensionW (wszPNGPath, L".png");

            if (SKIV_HDR_SavePNGToDisk (wszPNGPath, hdr_img.GetImages (), captured_img.GetImages (), nullptr))
            {
              if (SKIV_PNG_CopyToClipboard (*hdr_img.GetImages (), wszPNGPath, 0))
              {
                extern std::wstring dragDroppedFilePath;
                extern bool         activateSnipping;
                dragDroppedFilePath = wszPNGPath;

                activateSnipping = true;
              }
            }
          }
        }

      }

    break;

    // System wants to shut down and is asking if we can allow it
    case WM_QUERYENDSESSION:
      PLOG_INFO << "System in querying if we can shut down!";
      return true;
      break;

    case WM_ENDSESSION: 
      // Session is shutting down -- perform any last minute changes!
      if (wParam == 1)
      {
        PLOG_INFO << "Received system shutdown signal!";

        SKIF_Shutdown.store(true);
      }
      //return 0;
      break;

    case WM_QUIT:
      SKIF_Shutdown.store(true);
      break;

    case WM_SETTINGCHANGE:
      // ImmersiveColorSet is sent when either SystemUsesLightTheme (OS) or AppsUseLightTheme (apps) changes
      // If both are changed by the OS at the same time, two messages are sent to all apps
      if (lParam != NULL && _registry.iStyle == 0 && _wcsicmp (L"ImmersiveColorSet", reinterpret_cast<wchar_t*> (lParam)) == 0)
      {
        bool oldMode = _registry._StyleLightMode;

        ImGuiStyle            newStyle;
        SKIF_ImGui_SetStyle (&newStyle);

        // Only log and change the DWM borders if the color mode was actually changed
        if (oldMode != _registry._StyleLightMode)
        {
          PLOG_VERBOSE << "Detected a color change through a ImmersiveColorSet broadcast.";

          extern void
            SKIF_ImGui_ImplWin32_UpdateDWMBorders (void);
            SKIF_ImGui_ImplWin32_UpdateDWMBorders (    );
        }
      }

      break;

    case WM_POWERBROADCAST:
      if (wParam == PBT_APMSUSPEND)
      {
        // The system allows approximately two seconds for an application to handle this notification.
        // If an application is still performing operations after its time allotment has expired, the system may interrupt the application.
        PLOG_INFO << "System is suspending operation.";
      }

      // If the system wakes due to an external wake signal (remote wake), the system broadcasts only the PBT_APMRESUMEAUTOMATIC event.
      // The PBT_APMRESUMESUSPEND event is not sent.
      if (wParam == PBT_APMRESUMEAUTOMATIC)
      {
        PLOG_DEBUG << "Operation is resuming automatically from a low-power state.";
      }

      // If the system wakes due to user activity (such as pressing the power button) or if the system detects user interaction at the physical
      // console (such as mouse or keyboard input) after waking unattended, the system first broadcasts the PBT_APMRESUMEAUTOMATIC event, then
      // it broadcasts the PBT_APMRESUMESUSPEND event. In addition, the system turns on the display.
      // Your application should reopen files that it closed when the system entered sleep and prepare for user input.
      if (wParam == PBT_APMRESUMESUSPEND)
      {
        PLOG_INFO << "Operation is resuming from a low-power state due to user activity.";
        if (_registry.iCheckForUpdates == 2)
          SKIF_Updater::GetInstance ( ).CheckForUpdates ( );
      }

      break;

    case WM_GETICON: // Work around bug in Task Manager sending this message every time it refreshes its process list
      msgDontRedraw = true;
      return true;
      break;

    case WM_DISPLAYCHANGE:
      SKIF_Util_GetMonitorHzPeriod (SKIF_ImGui_hWnd, MONITOR_DEFAULTTONEAREST, dwDwmPeriod);

      if (SKIF_Tab_Selected == UITab_Settings)
        RefreshSettingsTab = true; // Only set this if the Settings tab is actually selected
      break;

    case WM_SKIF_MINIMIZE:
      if (SKIF_ImGui_hWnd != NULL)
        ShowWindowAsync (SKIF_ImGui_hWnd, SW_MINIMIZE);
      break;

    case WM_SKIF_REFRESHFOCUS:
      // Ensure the gamepad input thread knows what state we are actually in
      if (SKIF_ImGui_IsFocused ( ))
        _gamepad.WakeThread  ( );
      else
        _gamepad.SleepThread ( );
      break;

    case WM_SKIF_IMAGE:
      addAdditionalFrames += 3;

      // Update tryingToLoadCover
      extern bool tryingToLoadImage;
      extern std::atomic<bool> imageLoading;
      tryingToLoadImage = imageLoading.load();

      // Empty working set after the cover has finished loading
      if (! tryingToLoadImage)
      {
        SKIF_Util_CompactWorkingSet ( );

        bool success = static_cast<bool> (lParam);

        extern bool newImageLoaded;
        extern bool newImageFailed;
        extern bool tryingToDownImage;

        tryingToDownImage = false;

        if (success)
          newImageLoaded = true;
        else
          newImageFailed = true;
      }
      break;

    case WM_SKIF_REFRESHCOVER:
      {
        addAdditionalFrames += 3;

        // Update refreshCover
        extern bool         coverRefresh; // This just triggers a refresh of the cover
        extern std::wstring coverRefreshPath;
        extern int          coverRefreshCount;

        coverRefresh      = true;
        coverRefreshCount = ( int    )wParam;
        std::wstring* rePath = reinterpret_cast<std::wstring*>(lParam); // Requires using SendMessage only

        coverRefreshPath = *rePath;
      }
      break;

    case WM_SKIF_ICON:
      addAdditionalFrames += 3;
      break;

    case WM_SKIF_FILE_DIALOG:
      OpenFileDialog = PopupState_Open;
      break;

    case WM_SKIF_RUN_UPDATER:
      SKIF_Updater::GetInstance ( ).CheckForUpdates ( );
      break;

    case WM_SKIF_UPDATER:
      SKIF_Updater::GetInstance ( ).RefreshResults ( ); // Swap in the new results

      uFlags = (UpdateFlags)wParam;

      if (uFlags != UpdateFlags_Unknown)
      {
        // Only show the update prompt if we have a file downloaded and we either
        // forced it (switched channel) or it is not ignored nor an older version
        if ( (uFlags & UpdateFlags_Downloaded) == UpdateFlags_Downloaded &&
            ((uFlags & UpdateFlags_Forced)     == UpdateFlags_Forced     ||
            ((uFlags & UpdateFlags_Ignored)    != UpdateFlags_Ignored    &&
             (uFlags & UpdateFlags_Older)      != UpdateFlags_Older     )))
        {
          // If we use auto-update *experimental*
          // But only if we have servlets, so we don't auto-install ourselves in users' Downloads folder :)
          if (false && _registry.bAutoUpdate)
          {
            SKIF_Shell_CreateNotifyToast (SKIF_NTOAST_UPDATE, L"Blink and you'll miss it ;)", L"An update is being installed...");

            PLOG_INFO << "The app is performing an automatic update...";

            _registry.regKVAutoUpdateVersion.putData (SK_UTF8ToWideChar (SKIF_Updater::GetInstance ( ).GetResults ( ).version));
            
            std::wstring update = SK_FormatStringW (LR"(%ws\Version\%ws)", _path_cache.skiv_userdata, SKIF_Updater::GetInstance ( ).GetResults ( ).filename.c_str());
            std::wstring args   = SK_FormatStringW (LR"(/VerySilent /NoRestart /Shortcuts=false /DIR="%ws")", _path_cache.skiv_install);

            SKIF_Util_OpenURI (update.c_str(), SW_SHOWNORMAL, L"OPEN", args.c_str());
          }

          // Classic update procedure
          else {
            SKIF_Shell_CreateNotifyToast (SKIF_NTOAST_UPDATE, L"Open the app to continue.", L"An update to Special K is available!");

            PLOG_INFO << "An update is available!";

            UpdatePromptPopup = PopupState_Open;
          }
          addAdditionalFrames += 3;
        }

        else if ((uFlags & UpdateFlags_Failed) == UpdateFlags_Failed)
        {
          SKIF_Shell_CreateNotifyToast (SKIF_NTOAST_UPDATE, L"The update will be retried later.", L"Update failed :(");
        }
      }
      break;

    case WM_SKIF_RESTORE:
      if (SKIF_ImGui_hWnd != NULL)
      {
        if (! IsIconic (SKIF_ImGui_hWnd))
          RepositionSKIF            = true;

        ShowWindow     (SKIF_ImGui_hWnd, SW_RESTORE); // ShowWindowAsync

        if (! UpdateWindow        (SKIF_ImGui_hWnd))
          PLOG_DEBUG << "UpdateWindow ( ) failed!";

        if (! SetForegroundWindow (SKIF_ImGui_hWnd))
          PLOG_DEBUG << "SetForegroundWindow ( ) failed!";

        if (! SetActiveWindow     (SKIF_ImGui_hWnd))
          PLOG_DEBUG << "SetActiveWindow ( ) failed: "  << SKIF_Util_GetErrorAsWStr ( );

        if (! BringWindowToTop    (SKIF_ImGui_hWnd))
          PLOG_DEBUG << "BringWindowToTop ( ) failed: " << SKIF_Util_GetErrorAsWStr ( );
      }
      break;

    // Custom refresh window messages
    case WM_SKIF_POWERMODE:
      break;

    case WM_TIMER:
      addAdditionalFrames += 3;
      switch (wParam)
      {
        case IDT_REFRESH_NOTIFY:
          KillTimer (SKIF_Notify_hWnd, IDT_REFRESH_NOTIFY);
          break;
        case IDT_REFRESH_TOOLTIP:
          // Do not redraw if SKIF is not being hovered by the mouse or a hover tip is not longer "active" any longer
          if (! SKIF_ImGui_IsMouseHovered ( ) || ! HoverTipActive)
          {
            msgDontRedraw = true;
            addAdditionalFrames -= 3; // Undo the 3 frames we added just above
          }
          
          KillTimer (SKIF_Notify_hWnd, IDT_REFRESH_TOOLTIP);
          break;
        case IDT_REFRESH_GAMES: // TODO: Contemplate this design, and its position in the new design with situational pausing. Concerns WM_SKIF_REFRESHGAMES / IDT_REFRESH_GAMES.
          if (RepopulateGamesWasSet != 0 && RepopulateGamesWasSet + 1000 < SKIF_Util_timeGetTime())
          {
            RepopulateGamesWasSet = 0;
            KillTimer (SKIF_Notify_hWnd, IDT_REFRESH_GAMES);
          }
          break;
        // These are just dummy events to get SKIF to refresh for a couple of frames more periodically
        case cIDT_REFRESH_INJECTACK:
          //OutputDebugString(L"cIDT_REFRESH_INJECTACK\n");
          break;
        case cIDT_REFRESH_PENDING:
          //OutputDebugString(L"cIDT_REFRESH_PENDING\n");
          break;
        case  IDT_REFRESH_UPDATER:
          //OutputDebugString(L"IDT_REFRESH_UPDATER\n");
          break;
      }
      break;

    case WM_SYSCOMMAND:

      /*
      if ((wParam & 0xfff0) == SC_KEYMENU)
      {
        // Disable ALT application menu
        if ( lParam == 0x00 ||
             lParam == 0x20 )
        {
          return true;
        }
      }

      else if ((wParam & 0xfff0) == SC_MOVE)
      {
        // Disables the native move modal loop of Windows and
        // use the RepositionSKIF approach to move the window
        // to the center of the display the cursor is on.
        PostMessage (hWnd, WM_SKIF_RESTORE, 0x0, 0x0);
      }
      */
      break;

    case WM_DESTROY:
      ::PostQuitMessage (0);
      break;
  }
  
  // Tell the main thread to render at least three more frames after we have processed the message
  if (SKIF_ImGui_hWnd != NULL && ! msgDontRedraw)
  {
    addAdditionalFrames += 3;
    //PostMessage (SKIF_ImGui_hWnd, WM_NULL, 0, 0);
  }

  return 0;

  //return
  //  ::DefWindowProc (hWnd, msg, wParam, lParam);
}

LRESULT
WINAPI
SKIF_Notify_WndProc (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  // This is the message procedure for the notification icon window that also handles custom SKIF messages

  static SKIF_RegistrySettings&   _registry  = SKIF_RegistrySettings  ::GetInstance ( );

  PLOG_VERBOSE_IF(_registry.isDevLogging()) << std::format("[0x{:<4x}] [{:5d}] [{:20s}]{:s}[0x{:x}, {:d}{:s}] [0x{:x}, {:d}]",
                  msg, // Hexadecimal
                  msg, // Decimal
                  SKIF_Util_GetWindowMessageAsStr (msg), // String
                    (hWnd == SKIF_Notify_hWnd ?  " [SKIF_Notify_hWnd] " : " "), // Is the message meant SKIF_Notify_hWnd ?
                  wParam, wParam,
            ((HWND)wParam == SKIF_Notify_hWnd ?  ", SKIF_Notify_hWnd"   : ""),  // Does wParam point to SKIF_Notify_hWnd ?
                  lParam, lParam);

  if (SKIF_WndProc (hWnd, msg, wParam, lParam))
    return true;

  switch (msg)
  {
    case WM_SKIF_NOTIFY_ICON:
      msgDontRedraw = true; // Don't redraw the main window when we're interacting with the notification icon
      switch (lParam)
      {
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
          PostMessage (SKIF_Notify_hWnd, WM_SKIF_RESTORE, 0x0, 0x0);
          return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
          // Get current mouse position.
          POINT curPoint;
          GetCursorPos (&curPoint);

          // To display a context menu for a notification icon, the current window must be the foreground window
          // before the application calls TrackPopupMenu or TrackPopupMenuEx. Otherwise, the menu will not disappear
          // when the user clicks outside of the menu or the window that created the menu (if it is visible).
          SetForegroundWindow (hWnd);

          // TrackPopupMenu blocks the app until TrackPopupMenu returns
          TrackPopupMenu (
            hMenu,
            TPM_RIGHTBUTTON,
            curPoint.x,
            curPoint.y,
            0,
            hWnd,
            NULL
          );

          // However, when the current window is the foreground window, the second time this menu is displayed,
          // it appears and then immediately disappears. To correct this, you must force a task switch to the
          // application that called TrackPopupMenu. This is done by posting a benign message to the window or
          // thread, as shown in the following code sample:
          PostMessage (hWnd, WM_NULL, 0, 0);
          return 0;
        case NIN_BALLOONHIDE:
        case NIN_BALLOONSHOW:
        case NIN_BALLOONTIMEOUT:
        case NIN_BALLOONUSERCLICK:
        case NIN_POPUPCLOSE:
        case NIN_POPUPOPEN:
          break;
      }
      break;

    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case SKIF_NOTIFY_RUN_UPDATER:
          PostMessage (SKIF_Notify_hWnd, WM_SKIF_RUN_UPDATER, 0, 0);
          break;
        case SKIF_NOTIFY_EXIT:
          if (SKIF_ImGui_hWnd != NULL)
            PostMessage (SKIF_ImGui_hWnd, WM_CLOSE, 0, 0);
          break;
      }
      break;

    case WM_CREATE:
      SK_RunOnce (
        SHELL_TASKBAR_RESTART        = RegisterWindowMessage (TEXT ("TaskbarCreated"));
        SHELL_TASKBAR_BUTTON_CREATED = RegisterWindowMessage (TEXT ("TaskbarButtonCreated"));
      );
      break;
        
    default:
#if 0
      // Taskbar was recreated (explorer.exe restarted),
      //   so we need to recreate the notification icon
      if (msg == SHELL_TASKBAR_RESTART)
      {
        SKIF_Shell_DeleteNotifyIcon ( );
        SKIF_Shell_CreateNotifyIcon ( );
      }

      // When the taskbar button has been created,
      //   the icon overlay can be set accordingly
      if (msg == SHELL_TASKBAR_BUTTON_CREATED)
      {
        // Recreate things if needed
        SKIF_Shell_CreateUpdateNotifyMenu ( );
        SKIF_Shell_UpdateNotifyIcon       ( );
      }
#endif
      break;
  }
  return
    ::DefWindowProc (hWnd, msg, wParam, lParam);
}