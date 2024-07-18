//
// Copyright 2020 Andon "Kaldaien" Coleman
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

#include <atlbase.h>
#include <ShlObj.h>
#include <string>

bool
SK_FileHasSpaces    (const wchar_t *wszLongFileName);
bool
SK_FileHas8Dot3Name (const wchar_t *wszLongFileName);

bool
SK_Generate8Dot3    (const wchar_t *wszLongFileName);

HRESULT
ModifyPrivilege (
  IN LPCTSTR szPrivilege,
  IN BOOL    fEnable
);

// Cache of paths that do not change
struct SKIF_CommonPathsCache {
  struct win_path_s {
    KNOWNFOLDERID   folderid            = { };
    const wchar_t*  legacy_env_var      = L"";
    wchar_t         path [MAX_PATH + 2] = { };
    volatile LONG __init                =  0;
  };

  win_path_s my_documents       =
  {            FOLDERID_Documents,
    L"%USERPROFILE%\\My Documents"
  };
  win_path_s app_data_local     =
  {                  FOLDERID_LocalAppData,
    L"%USERPROFILE%\\AppData\\Local"
  };
  win_path_s app_data_local_low =
  {           FOLDERID_LocalAppDataLow,
    L"%USERPROFILE%\\AppData\\LocalLow"
  };
  win_path_s app_data_roaming   =
  {                  FOLDERID_RoamingAppData,
    L"%USERPROFILE%\\AppData\\Roaming"
  };
  win_path_s win_saved_games    =
  {          FOLDERID_SavedGames,
    L"%USERPROFILE%\\Saved Games"
  };
  win_path_s fonts              =
  {    FOLDERID_Fonts,
    L"%WINDIR%\\Fonts"
  };
  //win_path_s specialk_userdata  =
  //{            FOLDERID_Documents,
  //  L"%USERPROFILE%\\My Documents"
  //};
  win_path_s desktop            =
  {         FOLDERID_Desktop,
    L"%USERPROFILE%\\Desktop"
  };
  win_path_s user_startup       =
  {      FOLDERID_Startup,
    L"%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\StartUp"
  };

  wchar_t skiv_executable   [MAX_PATH + 2] = { }; // Holds the path to the executable
  wchar_t skiv_workdir_org  [MAX_PATH + 2] = { }; // Holds the original work directory (rarely used)
  wchar_t skiv_workdir      [MAX_PATH + 2] = { }; // Holds the adjusted work directory (rarely used)
  wchar_t skiv_install      [MAX_PATH + 2] = { }; // Holds the install folder for SKIV
  wchar_t skiv_userdata     [MAX_PATH + 2] = { }; // Holds the user data folder for SKIV
  wchar_t specialk_userdata [MAX_PATH + 2] = { }; // Holds the user data folder for SK (often lines up with its install folder)
  wchar_t skiv_temp         [MAX_PATH + 2] = { }; // Holds the temp data folder for SKIV (images downloaded from the web; cleared out on every launch): %APPDATA%\TEMP\SKIV\

  
  // Functions
  static SKIF_CommonPathsCache& GetInstance (void)
  {
      static SKIF_CommonPathsCache instance;
      return instance;
  }

  SKIF_CommonPathsCache (SKIF_CommonPathsCache const&) = delete; // Delete copy constructor
  SKIF_CommonPathsCache (SKIF_CommonPathsCache&&)      = delete; // Delete move constructor

private:
  SKIF_CommonPathsCache (void);
};

HRESULT
SK_Shell32_GetKnownFolderPath ( _In_ REFKNOWNFOLDERID rfid,
                                     std::wstring&     dir,
                            volatile LONG*             _RunOnce );

std::wstring
SK_GetFontsDir (void);

void
SKIF_GetFolderPath (SKIF_CommonPathsCache::win_path_s* path);

HRESULT
SK_FileOpenDialog (LPWSTR *pszPath, const COMDLG_FILTERSPEC* fileTypes, UINT cFileTypes, HWND hWndParent = 0, FILEOPENDIALOGOPTIONS dialogOptions = _FILEOPENDIALOGOPTIONS::FOS_FILEMUSTEXIST, const GUID defaultFolder = FOLDERID_StartMenu, const wchar_t* setFolder = nullptr);
HRESULT
SK_FileSaveDialog (LPWSTR *pszPath, LPCWSTR wszDefaultName, const wchar_t* wszDefaultExtension, const COMDLG_FILTERSPEC* fileTypes, UINT cFileTypes, HWND hWndParent = 0, FILEOPENDIALOGOPTIONS dialogOptions = FOS_STRICTFILETYPES|FOS_FILEMUSTEXIST|FOS_OVERWRITEPROMPT, const GUID defaultFolder = FOLDERID_StartMenu, const wchar_t* setFolder = nullptr);