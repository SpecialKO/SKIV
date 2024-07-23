#pragma once

#include <string>
#include <map>
#include <atomic>
#include <Windows.h>
#include <wtypes.h>
#include <WinInet.h>
#include <atlbase.h>
#include <Tlhelp32.h>
#include <processthreadsapi.h>
#include <vector>
#include <shellapi.h>
#include <stdexcept>
#include <DirectXTex.h>
#include "sk_utility.h"

#pragma comment(lib, "wininet.lib")

// Requires Windows 10, version 1703
//        + Per Monitor v2 DPI awareness
// 
// We use an opposite IF statement than
//   Microsoft's to get these defined
#if(WINVER < 0x0605)
#define WM_DPICHANGED_BEFOREPARENT      0x02E2
#define WM_DPICHANGED_AFTERPARENT       0x02E3
#define WM_GETDPISCALEDSIZE             0x02E4
#endif /* WINVER >= 0x0605 */


#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

// Stuff


extern UINT          CF_HTML;        // Registered clipboard format for the "HTML Format"
typedef unsigned int ClipboardData;  // -> enum ClipboardData_

enum ClipboardData_
{
  ClipboardData_None        = 0,
  ClipboardData_TextANSI    = 1 << 0, // Text        (ANSI)
  ClipboardData_TextUnicode = 1 << 1, // Text        (Unicode)
  ClipboardData_HTML        = 1 << 2, // HTML Format (ANSI)
  ClipboardData_Bitmap      = 1 << 3, // Bitmap
  ClipboardData_HDROP       = 1 << 4, // A list of files
};

enum UITab {
  UITab_None,
  UITab_Viewer,
  UITab_Settings,
  UITab_About,
  UITab_ALL      // Total number of elements in enum (technically against Microsoft's enum design guidelines, but whatever)
};

enum CaptureMode {
  CaptureMode_Window,
  CaptureMode_Region,
  CaptureMode_Screen
};

struct FileSignature {
  std::wstring               mime_type       = L"";
  std::vector <std::wstring> file_extensions = { };
  std::vector <uint8_t>      signature       = { };
  std::vector <uint8_t>      mask            = { };

  FileSignature (std::wstring m, std::vector <std::wstring> e, std::vector <uint8_t> s) : mime_type(m), file_extensions(e), signature(s)
  {
    // Fill the mask with 0xFF everywhere
    mask = std::vector <uint8_t> (signature.size(), 0xFF);
  };

  FileSignature (std::wstring m, std::vector <std::wstring> e, std::vector <uint8_t> s, std::vector <uint8_t> m2) : mime_type(m), file_extensions(e), signature(s), mask(m2)
  {
    if (mask.size() != signature.size())
      throw std::invalid_argument ("different sizes for signature and mask");
  };
};

constexpr  int     SKIF_HotKey_HDR           = 1337; // Win + Ctrl + Shift + H
constexpr  int     SKIF_HotKey_SVC           = 1338; // Win + Shift + Insert
constexpr  int     SKIV_HotKey_CaptureWindow = 1339; // Win + Ctrl + Shift + I
constexpr  int     SKIV_HotKey_CaptureRegion = 1340; // Win + Ctrl + Shift + O
constexpr  int     SKIV_HotKey_CaptureScreen = 1341; // Win + Ctrl + Shift + P

extern UITab       SKIF_Tab_Selected; // Current selected tab
extern UITab       SKIF_Tab_ChangeTo; // Tab we want to change to

extern std::vector<HANDLE> vWatchHandles[UITab_ALL];

// Generic Utilities

char *
__cdecl         SKIF_Util_FormatStringRaw             (char    const* const _Format, ...);
std:: string    SKIF_Util_ToLower                     (std:: string_view input);
std::wstring    SKIF_Util_ToLowerW                    (std::wstring_view input);
std:: string    SKIF_Util_ToUpper                     (std:: string_view input);
std::wstring    SKIF_Util_ToUpperW                    (std::wstring_view input);
void            SKIF_Util_CleanString                 (std:: string&     input);
void            SKIF_Util_CleanStringW                (std::wstring&     input);
void            SKIF_Util_StripNulls                  (std:: string&     input);
void            SKIF_Util_StripNullsW                 (std::wstring&     input);
void            SKIF_Util_StripCarriageReturns        (std:: string&     input);
void            SKIF_Util_StripCarriageReturnsW       (std::wstring&     input);
void            SKIF_Util_TrimSpaces                  (std:: string&     input);
void            SKIF_Util_TrimSpacesW                 (std::wstring&     input);
void            SKIF_Util_TrimLeadingSpaces           (std:: string&     input);
void            SKIF_Util_TrimLeadingSpacesW          (std::wstring&     input);
void            SKIF_Util_TrimTrailingSpaces          (std:: string&     input);
void            SKIF_Util_TrimTrailingSpacesW         (std::wstring&     input);
void            SKIF_Util_TrimNewLines                (std:: string&     input);
void            SKIF_Util_TrimNewLinesW               (std::wstring&     input);
void            SKIF_Util_TrimLeadingNewlines         (std:: string&     input);
void            SKIF_Util_TrimLeadingNewlinesW        (std::wstring&     input);
void            SKIF_Util_TrimTrailingNewlines        (std:: string&     input);
void            SKIF_Util_TrimTrailingNewlinesW       (std::wstring&     input);
std::wstring    SKIF_Util_GetErrorAsWStr              (DWORD error = GetLastError ( ), HMODULE module = NULL);
void            SKIF_Util_GetErrorAsMsgBox            (std::wstring winTitle = L"Error detected", std::wstring preMsg = L"", DWORD error = GetLastError ( ), HMODULE module = NULL);
DWORD           SKIF_Util_timeGetTime                 (void);
DWORD           SKIF_Util_timeGetTime1                (void); // Original non-cached data
std::wstring    SKIF_Util_timeGetTimeAsWStr           (time_t time);
std::wstring    SKIF_Util_timeGetTimeAsWStr           (const std::wstring& format = L"H:M:s.m");
int             SKIF_Util_CompareVersionStrings       (std:: string string1, std:: string string2);
int             SKIF_Util_CompareVersionStrings       (std::wstring string1, std::wstring string2);

// Filenames

std:: string    SKIF_Util_StripInvalidFilenameChars   (std:: string name);
std::wstring    SKIF_Util_StripInvalidFilenameChars   (std::wstring name);
std:: string    SKIF_Util_ReplaceInvalidFilenameChars (std:: string name,    char replacement);
std::wstring    SKIF_Util_ReplaceInvalidFilenameChars (std::wstring name, wchar_t replacement);
std:: string    SKIF_Util_NormalizeFullPath           (std:: string string);
std::wstring    SKIF_Util_NormalizeFullPath           (std::wstring string);
bool            SKIF_Util_HasFileSignature            (const std::vector<char>& header, const FileSignature& signature);
bool            SKIF_Util_HasFileExtension            (const std::wstring extension,    const FileSignature& signature);

// Usernames
std:: string    SKIF_Util_StripPersonalData           (std:: string input);
std::wstring    SKIF_Util_StripPersonalData           (std::wstring input);
void            SKIF_Util_Debug_LogUserNames          (void);


// ShellExecute

// Struct used to hold monitored data populated by worker threads
struct SKIF_Util_CreateProcess_s {
  uint32_t            id            =  0; // App ID
  int                 store_id      = -1;
  std::atomic<HANDLE> hWorkerThread = INVALID_HANDLE_VALUE; // Holds a handle to SKIF's worker thread servicing the request
  std::atomic<HANDLE> hProcess      = INVALID_HANDLE_VALUE; // Holds a handle to the spawned process
  std::atomic<DWORD>  dwProcessId   =  0;
  std::atomic<int>    iReturnCode   = -1; // Could the separate process be spawned through CreateProcess ? (0 == NO_ERROR; 1+ == ERROR_xxx)
};

HINSTANCE       SKIF_Util_ExplorePath                 (const std::wstring_view& path);
HINSTANCE       SKIF_Util_OpenURI                     (const std::wstring_view& path, int nShow = SW_SHOWNORMAL, LPCWSTR verb      = L"OPEN", LPCWSTR parameters = NULL, LPCWSTR directory = NULL, UINT flags = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOZONECHECKS);
bool            SKIF_Util_ShellExecuteUnelevated      (const std::wstring_view& path, int nShow = SW_SHOWNORMAL, LPCWSTR verb      = L"OPEN", LPCWSTR parameters = NULL, LPCWSTR directory = NULL);
bool            SKIF_Util_CreateProcess               (const std::wstring_view& path, const std::wstring_view& parameters, const std::wstring_view& directory, std::map<std::wstring, std::wstring>* env = nullptr, SKIF_Util_CreateProcess_s* proc = nullptr);


// Windows

typedef struct _SKIF_MEMORY_PRIORITY_INFORMATION {
  ULONG MemoryPriority;
} SKIF_MEMORY_PRIORITY_INFORMATION, *SKIF_PMEMORY_PRIORITY_INFORMATION;

HANDLE          SKIF_Util_GetCurrentProcess           (void);
HANDLE          SKIF_Util_GetCurrentProcessToken      (void);
BOOL            SKIF_Util_TerminateProcess            (DWORD  dwProcessId, UINT uExitCode);
BOOL    WINAPI  SKIF_Util_TerminateProcess            (HANDLE  hProcess,   UINT uExitCode);
std::wstring    SKIF_Util_GetFileVersion              (const wchar_t* wszName);
std::wstring    SKIF_Util_GetSpecialKDLLVersion       (const wchar_t* wszName);
std::wstring    SKIF_Util_GetProductName              (const wchar_t* wszName);
int             SKIF_Util_GetBinaryType               (const LPCTSTR pszPathToBinary);
BOOL    WINAPI  SKIF_Util_CompactWorkingSet           (void);
BOOL    WINAPI  SKIF_Util_GetSystemCpuSetInformation  (PSYSTEM_CPU_SET_INFORMATION Information, ULONG BufferLength, PULONG ReturnedLength, HANDLE Process, ULONG Flags);
bool            SKIF_Util_SetThreadPrefersECores      (void);
BOOL    WINAPI  SKIF_Util_SetThreadInformation        (HANDLE hThread,  THREAD_INFORMATION_CLASS ThreadInformationClass, LPVOID ThreadInformation, DWORD ThreadInformationSize);
HRESULT WINAPI  SKIF_Util_SetThreadDescription        (HANDLE hThread,  PCWSTR lpThreadDescription);
BOOL    WINAPI  SKIF_Util_SetThreadSelectedCpuSets    (HANDLE hThread,  const ULONG *CpuSetIds, ULONG CpuSetIdCount);
bool            SKIF_Util_SetThreadPowerThrottling    (HANDLE threadHandle, INT state);
bool            SKIF_Util_SetThreadMemoryPriority     (HANDLE threadHandle, ULONG memoryPriority);
bool            SKIF_Util_SetProcessPrefersECores     (void);
BOOL    WINAPI  SKIF_Util_SetProcessDefaultCpuSets    (HANDLE hProcess, const ULONG *CpuSetIds, ULONG CpuSetIdCount);
BOOL    WINAPI  SKIF_Util_SetProcessInformation       (HANDLE hProcess, PROCESS_INFORMATION_CLASS ProcessInformationClass, LPVOID ProcessInformation, DWORD ProcessInformationSize);
bool            SKIF_Util_SetProcessPowerThrottling   (HANDLE processHandle, INT state);
bool            SKIF_Util_SetProcessMemoryPriority    (HANDLE processHandle, ULONG memoryPriority);
bool            SKIF_Util_IsWindows8Point1OrGreater   (void);
bool            SKIF_Util_IsWindows10OrGreater        (void);
bool            SKIF_Util_IsWindows10v1709OrGreater   (void);
bool            SKIF_Util_IsWindows10v1903OrGreater   (void);
bool            SKIF_Util_IsWindows11orGreater        (void);
bool            SKIF_Util_IsWindowsVersionOrGreater   (DWORD dwMajorVersion, DWORD dwMinorVersion, DWORD dwBuildNumber);
bool            SKIF_Util_IsTouchCapable              (void);
bool            SKIF_Util_IsProcessAdmin              (DWORD PID);
bool            SKIF_Util_IsProcessX86                (HANDLE process);
PROCESSENTRY32W SKIF_Util_FindProcessByName           (const wchar_t* wszName);
DWORD           SKIF_Util_GetProcessIdFromHwnd        (HWND hwnd);
HANDLE          SKIF_Util_GetProcessHandleFromHwnd    (HWND hwnd, DWORD dwDesiredAccess);
bool            SKIF_Util_SaveExtractExeIcon          (std::wstring exePath, std::wstring targetPath);
bool            SKIF_Util_GetDragFromMaximized        (bool refresh = false);
bool            SKIF_Util_GetControlledFolderAccess   (void);
int             SKIF_Util_RegisterApp                 (bool force   = false);
bool            SKIF_Util_IsMPOsDisabledInRegistry    (bool refresh = false);
void            SKIF_Util_GetMonitorHzPeriod          (HWND hwnd, DWORD dwFlags, DWORD& dwPeriod);
bool            SKIF_Util_SetClipboardData            (const std::wstring_view& data);
std::string     SKIF_Util_GetClipboardTextData        (void);
std::wstring    SKIF_Util_GetClipboardTextDataW       (void);
std::string     SKIF_Util_GetClipboardHTMLData        (void);
std::wstring    SKIF_Util_GetClipboardHDROP           (void);
DirectX::Image  SKIF_Util_GetClipboardBitmapData      (void);
std::wstring    SKIF_Util_AddEnvironmentBlock         (const void* pEnvBlock, const std::wstring& varName, const std::wstring& varValue);
void            SKIF_Util_FileExplorer_SelectFile     (PCWSTR filePath);
std::string     SKIF_Util_GetWindowMessageAsStr       (UINT msg);


// Power Mode

std::string     SKIF_Util_GetEffectivePowerMode              (void);
void            SKIF_Util_SetEffectivePowerModeNotifications (bool enable);


// High Dynamic Range (HDR)

bool            SKIF_Util_IsHDRSupported              (HMONITOR hMonitor);
bool            SKIF_Util_IsHDRActive                 (HMONITOR hMonitor);
float           SKIF_Util_GetSDRWhiteLevel            (HMONITOR hMonitor);
bool            SKIF_Util_EnableHDROutput             (void);
void            SKIF_Util_UpdateMonitors              (void); // Triggers a refresh of the data that feeds all of the above; called by WM_DISPLAYCHANGE
bool            SKIF_Util_RegisterHotKeyHDRToggle     (const SK_Keybind* binding);
bool            SKIF_Util_UnregisterHotKeyHDRToggle   (void);
bool            SKIF_Util_GetHotKeyStateHDRToggle     (void);
bool            SKIF_Util_RegisterHotKeySVCTemp       (void);
bool            SKIF_Util_UnregisterHotKeySVCTemp     (void);
bool            SKIF_Util_GetHotKeyStateSVCTemp       (void);
bool            SKIF_Util_RegisterHotKeyCapture       (const CaptureMode mode, const SK_Keybind* binding);
bool            SKIF_Util_UnregisterHotKeyCapture     (const CaptureMode mode);
bool            SKIF_Util_GetHotKeyStateCapture       (const CaptureMode mode);


// Web

struct skif_get_web_uri_t {
  wchar_t wszHostName [INTERNET_MAX_HOST_NAME_LENGTH] = { };
  wchar_t wszHostPath [INTERNET_MAX_PATH_LENGTH]      = { };
  wchar_t wszExtraInfo[INTERNET_MAX_PATH_LENGTH]      = { };
  wchar_t wszLocalPath[MAX_PATH + 2]                  = { };
  LPCWSTR method                                      = L"GET";
  bool         https                                  = false;
  std::string  body;
  std::wstring header;
};

DWORD WINAPI SKIF_Util_GetWebUri              (skif_get_web_uri_t* get);
DWORD        SKIF_Util_GetWebResource         (std::wstring url, std::wstring_view destination, std::wstring method = L"GET", std::wstring header = L"", std::string body = "");
skif_get_web_uri_t SKIF_Util_CrackWebUrl      (const std::wstring url);


// Shortcuts (*.lnk)

void         SKIF_Util_ResolveShortcut       (HWND hwnd, LPCWSTR lpszLinkFile, LPWSTR lpszTarget, LPWSTR lpszArguments, int iPathBufferSize);
bool         SKIF_Util_CreateShortcut        (LPCWSTR lpszPathLink, LPCWSTR lpszTarget, LPCWSTR lpszArgs = L"\0", LPCWSTR lpszWorkDir = L"\0", LPCWSTR lpszDesc = L"\0", LPCWSTR lpszIconLocation = L"\0", int iIcon = 0);


// Directory Watch

// Both of these flags are required to properly detect new files when they have finished writing as well as deleted files
// FILE_NOTIFY_CHANGE_FILE_NAME  - Detects when files has been created, renamed, or deleted
// FILE_NOTIFY_CHANGE_LAST_WRITE - Detects when processes have finished writing to a file

struct SKIF_DirectoryWatch
{
  SKIF_DirectoryWatch  (void) { };
  SKIF_DirectoryWatch  (std::wstring_view wstrPath,
                                    UITab waitTab        = UITab_None,
                                     BOOL bWatchSubtree  = FALSE,
                                    DWORD dwNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
  ~SKIF_DirectoryWatch (void);

  bool isSignaled      (void) const;

  bool isSignaled      (std::wstring_view wstrPath,
                                    UITab waitTab        = UITab_None,
                                     BOOL bWatchSubtree  = FALSE,
                                    DWORD dwNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
  
  void reset           (void);

  HANDLE       _hChangeNotification = INVALID_HANDLE_VALUE; // If the FindFirstChangeNotification function fails, the return value is INVALID_HANDLE_VALUE.
  UITab        _waitTab             = UITab_None;
  std::wstring _path                = L"";

private:
  void registerNotify  (std::wstring_view wstrPath,
                                    UITab waitTab        = UITab_None,
                                     BOOL bWatchSubtree  = FALSE,
                                    DWORD dwNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
};


// Registry Watch

struct SKIF_RegistryWatch {
   SKIF_RegistryWatch (HKEY hRootKey,
             const wchar_t* wszSubKey,
             const wchar_t* wszEventName,
                       BOOL bWatchSubtree  = TRUE,
                      DWORD dwNotifyFilter = REG_NOTIFY_CHANGE_LAST_SET,
                      UITab waitTab        = UITab_None,
                       bool bWOW6432Key    = false,  // Access a 32-bit key from either a 32-bit or 64-bit application.
                       bool bWOW6464Key    = false); // Access a 64-bit key from either a 32-bit or 64-bit application.

  ~SKIF_RegistryWatch    (void);

  LSTATUS registerNotify (void) const;
  void reset             (void);
  bool isSignaled        (void);

  struct {
    HKEY         root        = { };
    std::wstring sub_key;
    BOOL         watch_subtree;
    DWORD        filter_mask;
    BOOL         wow64_32key; // Access a 32-bit key from either a 32-bit or 64-bit application.
    BOOL         wow64_64key; // Access a 64-bit key from either a 32-bit or 64-bit application.
  } _init;

  HKEY    _hKeyBase    = { };
  HANDLE  _hEvent      = NULL; // If the CreateEvent function fails, the return value is NULL.
  UITab   _waitTab     = UITab_None;
};

class SKIV_ScopedThreadPriority
{
public:
  SKIV_ScopedThreadPriority (int prio = THREAD_PRIORITY_TIME_CRITICAL) {
    orig_prio_ =
      GetThreadPriority (GetCurrentThread ());

    SetThreadPriority (GetCurrentThread  (), prio);
  };

  ~SKIV_ScopedThreadPriority (void) {
    SetThreadPriority (GetCurrentThread (), orig_prio_);
  }

private:
  int orig_prio_;
};
