//
// Copyright 2019-2022 Andon "Kaldaien" Coleman
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

#pragma once

#include <combaseapi.h>
#include <comdef.h>
#include <atlbase.h>

#include <string>
#include <string_view>

#ifndef PLOG_ENABLE_WCHAR_INPUT
#define PLOG_ENABLE_WCHAR_INPUT 1
#endif

#include <plog/Log.h>

// Constants - Timers / Window Messages / HotKeys / Windows

const     UINT_PTR       cIDT_REFRESH_INJECTACK =             1337; // Refresh every  500 ms if we expect an injection acknowledgement anytime soon **MAY BE REMOVED AGAIN -- Feels like an obsolete approach with SK posting window messages on events**
const     UINT_PTR       cIDT_REFRESH_PENDING   =             1338; // Refresh every 1000 ms since a new service state is pending
const     UINT_PTR        IDT_REFRESH_GAMES     =             1340;
const     UINT_PTR        IDT_REFRESH_TOOLTIP   =             1341;
const     UINT_PTR        IDT_REFRESH_UPDATER   =             1342;
const     UINT_PTR        IDT_REFRESH_NOTIFY    =             1343;
const     UINT_PTR        IDT_REFRESH_DIR_ROOT  =             1344; // Used by the directory watch for the root folder
const     UINT_PTR       cIDT_TIMER_EFFICIENCY  =             1345; // Engages Efficiency Mode (Win11) and compacts the working set
//const   UINT_PTR        IDT_REFRESH_STEAM_LIB =        1983-1999; // Used by the directory watch for Steam libraries

// Desktop notification types
constexpr UINT           SKIF_NTOAST_UPDATE     =                0; // Appears always
constexpr UINT           SKIF_NTOAST_SERVICE    =                1; // Appears conditionally

// Cmd line argument triggers
constexpr UINT           WM_SKIF_MINIMIZE       = WM_USER +  0x512;
constexpr UINT           WM_SKIF_RESTORE        = WM_USER +  0x513;
constexpr UINT           WM_SKIF_RUN_UPDATER    = WM_USER + 0x1029; // Triggers a check for updates
constexpr UINT           WM_SKIF_FILE_DIALOG    = WM_USER + 0x1030; // Opens a new file
constexpr UINT           WM_SKIF_SNIP_REGION    = WM_USER + 0x1031; // Capture a region
constexpr UINT           WM_SKIF_SNIP_FULLSCREEN= WM_USER + 0x1032; // Capture the fullscreen

// Thread workers
constexpr UINT           WM_SKIF_GAMEPAD        = WM_USER + 0x2049; // Gamepad input worker detected new input
constexpr UINT           WM_SKIF_IMAGE          = WM_USER + 0x2050; // Image worker completed
constexpr UINT           WM_SKIF_UPDATER        = WM_USER + 0x2051; // Updater worker completed
constexpr UINT           WM_SKIF_ICON           = WM_USER + 0x2052; // Patreon/Cover/Icon textures workers completed...
constexpr UINT           WM_SKIF_REFRESHCOVER   = WM_USER + 0x2053; // Refresh Cover -- Update Cover worker completed
constexpr UINT           WM_SKIF_REFRESHFOCUS   = WM_USER + 0x2054; // Trigger a new focus check from the main thread (used by child threads, e.g. gamepad input thread)

// Callbacks / Event Signals
constexpr UINT           WM_SKIF_POWERMODE      = WM_USER + 0x2101; // Used to signal that a new effective power mode has been applied

// Shell messages (registered window messages)
extern    UINT           SHELL_TASKBAR_RESTART;                     // TaskbarCreated
extern    UINT           SHELL_TASKBAR_BUTTON_CREATED;              // TaskbarButtonCreated

constexpr DWORD          SKIV_CDS_STRING        = 13;

//constexpr const  char*   SKIF_LOG_SEPARATOR     = "----------------------------";
constexpr const wchar_t* SKIF_NotifyIcoClass    = L"SKIV_NotificationIcon";
constexpr const wchar_t* SKIF_AppUserModelID    = L"SpecialK.Image.Viewer";

// Enums

enum PopupState {
  PopupState_Closed,
  PopupState_Open,
  PopupState_Opened
};

enum UIStyle {
  UIStyle_Dynamic,
  UIStyle_SKIF_Dark,
  UIStyle_SKIF_Light,
  UIStyle_ImGui_Classic,
  UIStyle_ImGui_Dark,
  UIStyle_COUNT     // Total number of elements in enum (technically against Microsoft's enum design guidelines, but whatever)
};

enum UIMode {
  UIMode_Normal,
  UIMode_VRR_Compatibility,
  UIMode_Safe_Mode,
  UIMode_COUNT     // Total number of elements in enum (technically against Microsoft's enum design guidelines, but whatever)
};

// Structs

struct SKIF_Signals { // Used for command line arguments
  BOOL Start                = FALSE;
  BOOL Temporary            = FALSE;
  BOOL Stop                 = FALSE;
  BOOL Quit                 = FALSE;
  BOOL Minimize             = FALSE;
//BOOL Restore              =  TRUE; // Only executed once
  BOOL AddSKIFGame          = FALSE;
  BOOL Launcher             = FALSE;
  BOOL LauncherURI          = FALSE;
  BOOL CheckForUpdates      = FALSE;
  BOOL OpenFileDialog       = FALSE;
  BOOL CaptureRegion        = FALSE;
  BOOL ServiceMode          = FALSE;

  // Helper variables
  HWND _RunningInstance     = NULL;
  std::wstring _FilePath    = L"";
};

// External declarations

extern PopupState  UpdatePromptPopup;   // App Mode: show an update prompt
extern PopupState  HistoryPopup;        // Monitor / About: show a changelog popup
extern PopupState  AutoUpdatePopup;     // Show changelog from the latest auto-installed update

extern HMODULE     hModSpecialK;        // Monitor: Used to dynamically load and unload the Special K DLL file when switching back and forth to the tab
extern HWND        SKIF_ImGui_hWnd;     // Main ImGui platform window (aka the main window of SKIF)
extern HWND        SKIF_Notify_hWnd;    // Notification area icon "window" that also doubles as a handler for the stuff previously tied to the now removed SKIF_hWnd 0x0 hidden window

extern float       SKIF_ImGui_GlobalDPIScale;
extern float       SKIF_ImGui_GlobalDPIScale_Last;
extern float       fBottomDist;

extern std::string SKIF_StatusBarText;
extern std::string SKIF_StatusBarHelp;

extern bool        RefreshSettingsTab;
