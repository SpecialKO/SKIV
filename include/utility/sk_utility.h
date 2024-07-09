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

#ifndef __SK__UTILITY_H__
#define __SK__UTILITY_H__

#define _CRT_NON_CONFORMING_SWPRINTFS

#include <Unknwnbase.h>

#include <intrin.h>
#include <Windows.h>

#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>
#include <bitset>
#include <array>
#include <string>

#ifndef PLOG_ENABLE_WCHAR_INPUT
#define PLOG_ENABLE_WCHAR_INPUT 1
#endif

#include <plog/Log.h>

#include <atlbase.h>

using HANDLE = void *;

template <typename T, typename T2, typename Q>
  __inline
  T
    static_const_cast ( const typename Q q )
    {
      return static_cast <T>  (
               const_cast <T2>  ( q )
                              );
    };

template <typename T, typename Q>
  __inline
  T**
    static_cast_p2p (     Q **      p2p ) noexcept
    {
      return static_cast <T **> (
               static_cast <T*>   ( p2p )
                                );
    };


enum SK_UNITS {
  Celsius    = 0,
  Fahrenheit = 1,
  B          = 2,
  KiB        = 3,
  MiB        = 4,
  GiB        = 5,
  Auto       = MAXDWORD
};


typedef enum DLL_ROLE : unsigned
{
  INVALID    = 0x000,


  // Graphics APIs
  DXGI       = 0x001, // D3D 10-12
  D3D9       = 0x002,
  OpenGL     = 0x004, // All versions
  Vulkan     = 0x008,
  D3D11      = 0x010, // Explicitly d3d11.dll
  D3D11_CASE = 0x011, // For use in switch statements
  D3D12      = 0x020, // Explicitly d3d12.dll

  DInput8    = 0x100,

  // Third-party Wrappers (i.e. dgVoodoo2)
  // -------------------------------------
  //
  //  Special K merely exports the correct symbols
  //    for binary compatibility; it has no native
  //      support for rendering in these APIs.
  //
  D3D8       = 0xC0000010,
  DDraw      = 0xC0000020,
  Glide      = 0xC0000040, // All versions


  // Behavior Flags
  PlugIn     = 0x00010000, // Stuff like Tales of Zestiria "Fix"
  Wrapper    = 0x40000000,
  ThirdParty = 0x80000000,

  DWORDALIGN = MAXDWORD
} DLL_ROLE;


//
// NOTE: Most of these functions are not intended to be DLL exported, so returning and
//         passing std::wstring is permissible for convenience.
//

const wchar_t* __stdcall
               SK_GetRootPath               (void);
const wchar_t* SK_GetHostApp                (void);
const wchar_t* SK_GetHostPath               (void);
const wchar_t* SK_GetBlacklistFilename      (void);

bool           SK_GetDocumentsDir           (_Out_opt_ wchar_t* buf, _Inout_ uint32_t* pdwLen);
std::wstring&  SK_GetDocumentsDir           (void);
std::wstring   SK_GetFontsDir               (void);
std::wstring   SK_GetRTSSInstallDir         (void);
bool
__stdcall      SK_CreateDirectories         (const wchar_t* wszPath);
uint64_t       SK_DeleteTemporaryFiles      (const wchar_t* wszPath    = SK_GetHostPath (),
                                             const wchar_t* wszPattern = L"SKI*.tmp");
std::wstring   SK_EvalEnvironmentVars       (const wchar_t* wszEvaluateMe);
bool           SK_GetUserProfileDir         (wchar_t*       buf, uint32_t* pdwLen);
bool           SK_IsTrue                    (const wchar_t* string);
bool           SK_IsAdmin                   (void);
void           SK_ElevateToAdmin            (void); // Needs DOS 8.3 filename support
void           SK_RestartGame               (const wchar_t* wszDLL = nullptr);
int            SK_MessageBox                (std::wstring caption,
                                             std::wstring title,
                                             uint32_t     flags);

LPVOID         SK_Win32_GetTokenInfo        (_TOKEN_INFORMATION_CLASS tic);
LPVOID         SK_Win32_ReleaseTokenInfo    (LPVOID                   lpTokenBuf);

time_t         SK_Win32_FILETIME_to_time_t  (FILETIME const& ft);
FILETIME       SK_Win32_time_t_to_FILETIME  (time_t          epoch);

std::string    SK_WideCharToUTF8            (const std::wstring& in);
std::wstring   SK_UTF8ToWideChar            (const std::string&  in);

std::string
__cdecl        SK_FormatString              (char    const* const _Format, ...);
std::wstring
__cdecl        SK_FormatStringW             (wchar_t const* const _Format, ...);
int
__cdecl        SK_FormatString              (std::string& out, char const* const _Format, ...);
int
__cdecl        SK_FormatStringW             (std::wstring& out, wchar_t const* const _Format, ...);

void           SK_StripTrailingSlashesW     (wchar_t *wszInOut);
void           SK_StripTrailingSlashesA     (char    *szInOut);
void           SK_FixSlashesW               (wchar_t *wszInOut);
void           SK_FixSlashesA               (char    *szInOut);
void           SK_StripLeadingSlashesW      (wchar_t *wszInOut);
void           SK_StripLeadingSlashesA      (char    *szInOut);

void           SK_File_SetNormalAttribs     (const wchar_t* file);
void           SK_File_MoveNoFail           (const wchar_t* wszOld,    const wchar_t* wszNew);
void           SK_File_FullCopy             (const wchar_t* from,      const wchar_t* to);
BOOL           SK_File_SetAttribs           (const wchar_t* file,      DWORD          dwAttribs);
BOOL           SK_File_ApplyAttribMask      (const wchar_t* file,      DWORD          dwAttribMask,
                                             bool           clear = false);
BOOL           SK_File_SetHidden            (const wchar_t* file,      bool           hidden);
BOOL           SK_File_SetTemporary         (const wchar_t* file,      bool           temp);
uint64_t       SK_File_GetSize              (const wchar_t* wszFile);
std::wstring   SK_File_SizeToString         (uint64_t       size,      SK_UNITS       unit = Auto);
std::string    SK_File_SizeToStringA        (uint64_t       size,      SK_UNITS       unit = Auto);
std::wstring   SK_File_SizeToStringF        (uint64_t       size,      int            width,
                                             int            precision, SK_UNITS       unit = Auto);
std::string    SK_File_SizeToStringAF       (uint64_t       size,      int            width,
                                             int            precision, SK_UNITS       unit = Auto);
bool           SK_File_IsDirectory          (const wchar_t* wszPath);
bool           SK_File_CanUserWriteToPath   (const wchar_t* wszPath);

std::wstring   SK_SYS_GetInstallPath        (void);

const wchar_t* SK_GetHostApp                (void);
const wchar_t* SK_GetSystemDirectory        (void);

#pragma intrinsic (_ReturnAddress)

HMODULE        SK_GetCallingDLL             (LPCVOID pReturn = _ReturnAddress ());
std::wstring   SK_GetCallerName             (LPCVOID pReturn = _ReturnAddress ());
HMODULE        SK_GetModuleFromAddr         (LPCVOID addr) noexcept;
std::wstring   SK_GetModuleName             (HMODULE hDll);
std::wstring   SK_GetModuleFullName         (HMODULE hDll);
std::wstring   SK_GetModuleNameFromAddr     (LPCVOID addr);
std::wstring   SK_GetModuleFullNameFromAddr (LPCVOID addr);
std::wstring   SK_MakePrettyAddress         (LPCVOID addr, DWORD dwFlags = 0x0);
bool           SK_ValidatePointer           (LPCVOID addr, bool silent = false);
bool           SK_IsAddressExecutable       (LPCVOID addr, bool silent = false);
void           SK_LogSymbolName             (LPCVOID addr);

FARPROC WINAPI SK_GetProcAddress            (      HMODULE  hMod,      const char* szFunc) noexcept;
FARPROC WINAPI SK_GetProcAddress            (const wchar_t* wszModule, const char* szFunc);


std::wstring
        __stdcall
               SK_GetDLLVersionStr          (const wchar_t* wszName);

const wchar_t*
        __stdcall
               SK_GetCanonicalDLLForRole    (enum DLL_ROLE role);

const wchar_t* SK_DescribeHRESULT           (HRESULT hr);

void           SK_DeferCommand              (const char* szCommand);

void           SK_GetSystemInfo             (LPSYSTEM_INFO lpSystemInfo);

PSID SK_Win32_GetTokenSid     (_TOKEN_INFORMATION_CLASS tic );
PSID SK_Win32_ReleaseTokenSid (PSID                     pSid);


class SK_AutoHandle : public CHandle
{
  // Signed handles are invalid, since handles are pointers and
  //   the signed half of the address space is only for kernel

public:
   SK_AutoHandle (HANDLE hHandle) noexcept : CHandle (hHandle) { };
  ~SK_AutoHandle (void)           noexcept
  {
    // We cannot close these handles because technically they
    //   were never opened (by usermode code).
    if ((intptr_t)m_h < (intptr_t)nullptr)
                  m_h =           nullptr;

    // Signed handles are often special cases
    //   such as -2 = Current Thread, -1 = Current Process
  }

  const HANDLE& get (void) const noexcept { return m_h; };
};



extern void WINAPI  SK_ExitProcess      (      UINT      uExitCode  ) noexcept;
extern void WINAPI  SK_ExitThread       (      DWORD     dwExitCode ) noexcept;
extern BOOL WINAPI  SK_TerminateThread  (      HANDLE    hThread,
                                               DWORD     dwExitCode ) noexcept;
extern BOOL WINAPI  SK_TerminateProcess (      HANDLE    hProcess,
                                               UINT      uExitCode  ) noexcept;
extern void __cdecl SK__endthreadex     ( _In_ unsigned _ReturnCode ) noexcept;


constexpr int
__stdcall
SK_GetBitness (void)
{
#ifdef _M_AMD64
  return 64;
#else
  return 32;
#endif
}

// Avoid the C++ stdlib and use CPU interlocked instructions instead, so this
//   is safe to use even by parts of the DLL that run before the CRT initializes
#define SK_RunOnce(x)    { static volatile LONG __once = TRUE; \
               if (InterlockedCompareExchange (&__once, FALSE, TRUE)) { x; } }
static inline auto
        SK_RunOnceEx =
              [](auto x){ static std::once_flag the_wuncler;
                            std::call_once (    the_wuncler, [&]{ (x)(); }); };

#define SK_RunIf32Bit(x)         { SK_GetBitness () == 32  ? (x) :  0; }
#define SK_RunIf64Bit(x)         { SK_GetBitness () == 64  ? (x) :  0; }
#define SK_RunLHIfBitness(b,l,r)   SK_GetBitness () == (b) ? (l) : (r)


#define SK_LOG_FIRST_CALL { bool called = false;                                       \
                     SK_RunOnce (called = true);                                       \
                             if (called) {                                             \
        SK_LOG0 ( (L"[!] > First Call: %34hs", __FUNCTION__),       __SK_SUBSYSTEM__); \
        SK_LOG1 ( (L"    <*> %s", SK_SummarizeCaller ().c_str ()), __SK_SUBSYSTEM__); } }


void SK_ImGui_Warning          (const wchar_t* wszMessage);
void SK_ImGui_WarningWithTitle (const wchar_t* wszMessage,
                                const wchar_t* wszTitle);

#define SK_ReleaseAssertEx(_expr,_msg,_file,_line,_func) {                \
  if (! (_expr))                                                          \
  {                                                                       \
    SK_LOG0 ( (  L"Critical Assertion Failure: '%ws' (%ws:%u) -- %hs",    \
                   (_msg), (_file), (_line), (_func)                      \
              ), L" SpecialK ");                                          \
                                                                          \
    if (config.system.log_level > 1)                                      \
    {                                                                     \
      if (SK_IsDebuggerPresent ())                                        \
             __debugbreak      ();                                        \
    }                                                                     \
                                                                          \
    else                                                                  \
    { SK_RunOnce (                                                        \
         SK_ImGui_WarningWithTitle (                                      \
                  SK_FormatStringW ( L"Critical Assertion Failure: "      \
                                     L"'%ws'\r\n\r\n\tFunction: %hs"      \
                                               L"\r\n\tSource: (%ws:%u)", \
                                       (_msg),  (_func),                  \
                                       (_file), (_line)                   \
                                   ).c_str (),                            \
                                 L"First-Chance Assertion Failure" )      \
                 );                }                               }      }

#define SK_ReleaseAssert(expr) { SK_ReleaseAssertEx ( (expr),L#expr,    \
                                                    __FILEW__,__LINE__, \
                                                    __FUNCSIG__ ) }


std::queue <DWORD>
               SK_SuspendAllOtherThreads (void);
void
               SK_ResumeThreads          (std::queue <DWORD> threads);


bool __cdecl   SK_IsRunDLLInvocation     (void);
bool __cdecl   SK_IsSuperSpecialK        (void);


// TODO: Push the SK_GetHostApp (...) stuff into this class
class SK_HostAppUtil
{
public:
  void         init                      (void);

  bool         isInjectionTool           (void)
  {
    init ();

    return SKIF || SKIM || (RunDll32 && SK_IsRunDLLInvocation ());
  }


protected:
  bool        SKIM     = false;
  bool        SKIF     = false;
  bool        RunDll32 = false;
};

SK_HostAppUtil*
SK_GetHostAppUtil (void);

bool __stdcall SK_IsDLLSpecialK          (const wchar_t* wszName);
void __stdcall SK_SelfDestruct           (void) noexcept;



struct sk_import_test_s {
  const char* szModuleName;
  bool        used;
};

void __stdcall SK_TestImports          (HMODULE hMod, sk_import_test_s* pTests, int nCount);

//
// This prototype is now completely ridiculous, this "design" sucks...
//   FIXME!!
//
void
SK_TestRenderImports ( HMODULE hMod,
                       bool*   gl,
                       bool*   vulkan,
                       bool*   d3d9,
                       bool*   dxgi,
                       bool*   d3d11,
                       bool*   d3d8,
                       bool*   ddraw,
                       bool*   glide );

void
__stdcall
SK_wcsrep ( const wchar_t*   wszIn,
                  wchar_t** pwszOut,
            const wchar_t*   wszOld,
            const wchar_t*   wszNew );


const wchar_t*
SK_Path_wcsrchr (const wchar_t* wszStr, wchar_t wchr);

const wchar_t*
SK_Path_wcsstr (const wchar_t* wszStr, const wchar_t* wszSubStr);

int
SK_Path_wcsicmp (const wchar_t* wszStr1, const wchar_t* wszStr2);

size_t
SK_RemoveTrailingDecimalZeros (wchar_t* wszNum, size_t bufLen = 0);

size_t
SK_RemoveTrailingDecimalZeros (char* szNum, size_t bufLen = 0);


void*
__stdcall
SK_Scan         (const void* pattern, size_t len, const void* mask);

void*
__stdcall
SK_ScanAligned (const void* pattern, size_t len, const void* mask, int align = 1);

void*
__stdcall
SK_ScanAlignedEx (const void* pattern, size_t len, const void* mask, void* after = nullptr, int align = 1);

BOOL
__stdcall
SK_InjectMemory ( LPVOID  base_addr,
            const void   *new_data,
                  size_t  data_size,
                  DWORD   permissions,
                  void   *old_data     = nullptr );

bool
SK_IsProcessRunning (const wchar_t* wszProcName);


// Call this instead of ShellExecuteW in order to handle COM init. so that all
// verbs (lpOperation) work.
HINSTANCE
WINAPI
SK_ShellExecuteW ( _In_opt_ HWND    hwnd,
                   _In_opt_ LPCWSTR lpOperation,
                   _In_     LPCWSTR lpFile,
                   _In_opt_ LPCWSTR lpParameters,
                   _In_opt_ LPCWSTR lpDirectory,
                   _In_     INT     nShowCmd );

HINSTANCE
WINAPI
SK_ShellExecuteA ( _In_opt_ HWND   hwnd,
                   _In_opt_ LPCSTR lpOperation,
                   _In_     LPCSTR lpFile,
                   _In_opt_ LPCSTR lpParameters,
                   _In_opt_ LPCSTR lpDirectory,
                   _In_     INT    nShowCmd );


constexpr size_t
hash_string (const wchar_t* const wstr, bool lowercase = false)
{
  if (wstr == nullptr)
    return 0;

  // Obviously this is completely blind to locale, but it does what
  //   is required while keeping the entire lambda constexpr-valid.
  constexpr auto constexpr_towlower = [&](const wchar_t val) ->
  wchar_t
  {
    return
      ( ( val >= L'A'  &&
          val <= L'Z' ) ? val + 32 :
                          val );

  };

  auto __h =
    size_t { 0 };

  for ( auto ptr = wstr ; *ptr != L'\0' ; ++ptr )
  {
    __h =
      ( lowercase ? constexpr_towlower (*ptr) :
                                        *ptr  )
               +
   (__h << 06) + (__h << 16) -
    __h;
  }

  return
    __h;
}

constexpr size_t
hash_string_utf8 (const char* const ustr, bool lowercase = false)
{
  if (ustr == nullptr)
    return 0;

  // Obviously this is completely blind to locale, but it does what
  //   is required while keeping the entire lambda constexpr-valid.
  constexpr auto constexpr_tolower = [&](const char val) ->
  char
  {
    return
      ( ( val >= 'A'  &&
          val <= 'Z' ) ? val + 32 :
                         val );
  };

  auto __h =
    size_t { 0 };

  for ( auto ptr = ustr ; *ptr != '\0' ; ++ptr )
  {
    __h =
      ( lowercase ? constexpr_tolower (*ptr) :
                                       *ptr  )
               +
   (__h << 06) + (__h << 16) -
    __h;
  }

  return
    __h;
}



class InstructionSet
{
  // Fwd decl
  class InstructionSet_Internal;

public:
  // Accessors
  //
  static std::string Vendor (void)          { return CPU_Rep->vendor_;        }
  static std::string Brand  (void)          { return CPU_Rep->brand_;         }

  static int  Family        (void) noexcept { return CPU_Rep->family_;        }
  static int  Model         (void) noexcept { return CPU_Rep->model_;         }
  static int  Stepping      (void) noexcept { return CPU_Rep->stepping_;      }

  static bool SSE3          (void)          { return CPU_Rep->f_1_ECX_  [ 0]; }
  static bool PCLMULQDQ     (void)          { return CPU_Rep->f_1_ECX_  [ 1]; }
  static bool MONITOR       (void)          { return CPU_Rep->f_1_ECX_  [ 3]; }
  static bool SSSE3         (void)          { return CPU_Rep->f_1_ECX_  [ 9]; }
  static bool FMA           (void)          { return CPU_Rep->f_1_ECX_  [12]; }
  static bool CMPXCHG16B    (void)          { return CPU_Rep->f_1_ECX_  [13]; }
  static bool SSE41         (void)          { return CPU_Rep->f_1_ECX_  [19]; }
  static bool SSE42         (void)          { return CPU_Rep->f_1_ECX_  [20]; }
  static bool MOVBE         (void)          { return CPU_Rep->f_1_ECX_  [22]; }
  static bool POPCNT        (void)          { return CPU_Rep->f_1_ECX_  [23]; }
  static bool AES           (void)          { return CPU_Rep->f_1_ECX_  [25]; }
  static bool XSAVE         (void)          { return CPU_Rep->f_1_ECX_  [26]; }
  static bool OSXSAVE       (void)          { return CPU_Rep->f_1_ECX_  [27]; }
  static bool AVX           (void)          { return CPU_Rep->f_1_ECX_  [28]; }
  static bool F16C          (void)          { return CPU_Rep->f_1_ECX_  [29]; }
  static bool RDRAND        (void)          { return CPU_Rep->f_1_ECX_  [30]; }

  static bool MSR           (void)          { return CPU_Rep->f_1_EDX_  [ 5]; }
  static bool CX8           (void)          { return CPU_Rep->f_1_EDX_  [ 8]; }
  static bool SEP           (void)          { return CPU_Rep->f_1_EDX_  [11]; }
  static bool CMOV          (void)          { return CPU_Rep->f_1_EDX_  [15]; }
  static bool CLFSH         (void)          { return CPU_Rep->f_1_EDX_  [19]; }
  static bool MMX           (void)          { return CPU_Rep->f_1_EDX_  [23]; }
  static bool FXSR          (void)          { return CPU_Rep->f_1_EDX_  [24]; }
  static bool SSE           (void)          { return CPU_Rep->f_1_EDX_  [25]; }
  static bool SSE2          (void)          { return CPU_Rep->f_1_EDX_  [26]; }

  static bool FSGSBASE      (void)          { return CPU_Rep->f_7_EBX_  [ 0]; }
  static bool BMI1          (void)          { return CPU_Rep->f_7_EBX_  [ 3]; }
  static bool HLE           (void)          { return CPU_Rep->isIntel_  &&
                                                     CPU_Rep->f_7_EBX_  [ 4]; }
  static bool AVX2          (void)          { return CPU_Rep->f_7_EBX_  [ 5]; }
  static bool BMI2          (void)          { return CPU_Rep->f_7_EBX_  [ 8]; }
  static bool ERMS          (void)          { return CPU_Rep->f_7_EBX_  [ 9]; }
  static bool INVPCID       (void)          { return CPU_Rep->f_7_EBX_  [10]; }
  static bool RTM           (void)          { return CPU_Rep->isIntel_  &&
                                                     CPU_Rep->f_7_EBX_  [11]; }
  static bool AVX512F       (void)          { return CPU_Rep->f_7_EBX_  [16]; }
  static bool RDSEED        (void)          { return CPU_Rep->f_7_EBX_  [18]; }
  static bool ADX           (void)          { return CPU_Rep->f_7_EBX_  [19]; }
  static bool AVX512PF      (void)          { return CPU_Rep->f_7_EBX_  [26]; }
  static bool AVX512ER      (void)          { return CPU_Rep->f_7_EBX_  [27]; }
  static bool AVX512CD      (void)          { return CPU_Rep->f_7_EBX_  [28]; }
  static bool SHA           (void)          { return CPU_Rep->f_7_EBX_  [29]; }

  static bool PREFETCHWT1   (void)          { return CPU_Rep->f_7_ECX_  [ 0]; }

  static bool LAHF          (void)          { return CPU_Rep->f_81_ECX_ [ 0]; }
  static bool LZCNT         (void)          { return CPU_Rep->isIntel_ &&
                                                     CPU_Rep->f_81_ECX_ [ 5]; }
  static bool ABM           (void)          { return CPU_Rep->isAMD_   &&
                                                     CPU_Rep->f_81_ECX_ [ 5]; }
  static bool SSE4a         (void)          { return CPU_Rep->isAMD_   &&
                                                     CPU_Rep->f_81_ECX_ [ 6]; }
  static bool XOP           (void)          { return CPU_Rep->isAMD_   &&
                                                     CPU_Rep->f_81_ECX_ [11]; }
  static bool TBM           (void)          { return CPU_Rep->isAMD_   &&
                                                     CPU_Rep->f_81_ECX_ [21]; }

  static bool SYSCALL       (void)          { return CPU_Rep->isIntel_ &&
                                                     CPU_Rep->f_81_EDX_ [11]; }
  static bool MMXEXT        (void)          { return CPU_Rep->isAMD_   &&
                                                     CPU_Rep->f_81_EDX_ [22]; }
  static bool RDTSCP        (void)          { return CPU_Rep->isIntel_ &&
                                                     CPU_Rep->f_81_EDX_ [27]; }
  static bool _3DNOWEXT     (void)          { return CPU_Rep->isAMD_   &&
                                                     CPU_Rep->f_81_EDX_ [30]; }
  static bool _3DNOW        (void)          { return CPU_Rep->isAMD_   &&
                                                     CPU_Rep->f_81_EDX_ [31]; }

  static void deferredInit  (void)          { SK_RunOnce (CPU_Rep = std::make_unique <InstructionSet_Internal> ()); }

private:
  static std::unique_ptr <InstructionSet_Internal> CPU_Rep;

  class InstructionSet_Internal
  {
  public:
    InstructionSet_Internal (void) : nIds_     { 0     }, nExIds_   { 0     },
                                     vendor_   ( ""    ), brand_    ( ""    ),
                                     family_   { 0     }, model_    { 0     },
                                     stepping_ { 0     },
                                     isIntel_  { false }, isAMD_    { false },
                                     f_1_ECX_  { 0     }, f_1_EDX_  { 0     },
                                     f_7_EBX_  { 0     }, f_7_ECX_  { 0     },
                                     f_81_ECX_ { 0     }, f_81_EDX_ { 0     }
    {
      //int cpuInfo[4] = {-1};
      std::array <int, 4> cpui;

      // Calling __cpuid with 0x0 as the function_id argument
      // gets the number of the highest valid function ID.

      __cpuid (cpui.data (), 0);
       nIds_ = cpui [0];

      for (int i = 0; i <= nIds_; ++i)
      {
        __cpuidex          (cpui.data (), i, 0);
        data_.emplace_back (cpui);
      }

      // Capture vendor string
      //
      int vendor  [8] = { };

      if (nIds_ >= 0)
      {
        * vendor      = data_ [0][1];
        *(vendor + 1) = data_ [0][3];
        *(vendor + 2) = data_ [0][2];
      }

      vendor_ =
        reinterpret_cast <char *> (vendor); //-V206

           if  (vendor_ == "GenuineIntel")  isIntel_ = true;
      else if  (vendor_ == "AuthenticAMD")  isAMD_   = true;

      if (nIds_ >= 1)
      {
        stepping_ =  data_ [1][0]       & 0xF;
        model_    = (data_ [1][0] >> 4) & 0xF;
        family_   = (data_ [1][0] >> 8) & 0xF;

        // Load Bitset with Flags for Function 0x00000001
        //
        f_1_ECX_ = data_ [1][2];
        f_1_EDX_ = data_ [1][3];
      }

      // Load Bitset with Flags for Function 0x00000007
      //
      if (nIds_ >= 7)
      {
        f_7_EBX_ = data_ [7][1];
        f_7_ECX_ = data_ [7][2];
      }

      // Calling __cpuid with 0x80000000 as the function_id argument
      // gets the number of the highest valid extended ID.
      //
       __cpuid (cpui.data ( ), 0x80000000);
      nExIds_ = cpui      [0];

      for (int i = 0x80000000; i <= nExIds_; ++i)
      {
        __cpuidex          (cpui.data (), i, 0);
        extdata_.push_back (cpui);
      }

      // Load Bitset with Flags for Function 0x80000001
      //
      if (nExIds_ >= 0x80000001)
      {
        f_81_ECX_ = extdata_ [1][2];
        f_81_EDX_ = extdata_ [1][3];
      }

      // Interpret CPU Brand String if Reported
      if (nExIds_ >= 0x80000004)
      {
        char    brand [0x40] =
        {                    };
        memcpy (brand,      extdata_ [2].data (), sizeof (cpui));
        memcpy (brand + 16, extdata_ [3].data (), sizeof (cpui));
        memcpy (brand + 32, extdata_ [4].data (), sizeof (cpui));

        brand_ = brand;
      }
    };

                             int       nIds_;
                             int       nExIds_;
                      std::string      vendor_;
                      std::string      brand_;
                     unsigned int      family_;
                     unsigned int      model_;
                     unsigned int      stepping_;
                             bool      isIntel_;
                             bool      isAMD_;
                      std::bitset <32> f_1_ECX_;
                      std::bitset <32> f_1_EDX_;
                      std::bitset <32> f_7_EBX_;
                      std::bitset <32> f_7_ECX_;
                      std::bitset <32> f_81_ECX_;
                      std::bitset <32> f_81_EDX_;
    std::vector < std::array <
                  int,     4 >
                >                      data_;
    std::vector < std::array <
                  int,     4 >
                >                      extdata_;
  };
};

auto constexpr CountSetBits = [](ULONG_PTR bitMask) noexcept ->
DWORD
{
  constexpr
        DWORD     LSHIFT      = sizeof (ULONG_PTR) * 8 - 1;
        DWORD     bitSetCount = 0;
        ULONG_PTR bitTest     =        (ULONG_PTR)1 << LSHIFT;
        DWORD     i;

  for (i = 0; i <= LSHIFT; ++i)
  {
    bitSetCount += ((bitMask & bitTest) ? 1 : 0);
    bitTest     /= 2;
  }

  return
    bitSetCount;
};

// The underlying function (PathCombineW) has unexpected behavior if pszFile
//   begins with a slash, so it is best to call this wrapper and we will take
//     care of the leading slash.
LPWSTR SK_PathCombineW ( _Out_writes_ (MAX_PATH) LPWSTR pszDest,
                                       _In_opt_ LPCWSTR pszDir,
                                       _In_opt_ LPCWSTR pszFile );


extern
void WINAPI
SK_Sleep (DWORD dwMilliseconds) noexcept;

template < class T,
           class ...    Args > std::unique_ptr <T>
SK_make_unique_nothrow (Args && ... args) noexcept
(                                         noexcept
(  T ( std::forward   < Args >     (args)   ... ))
);

// Keybindings

struct SK_Keybind
{
  const char*  bind_name           = nullptr;
  std::wstring human_readable      =     L"";
  std:: string human_readable_utf8 =      ""; // Read-only UTF8 copy

  struct {
    BOOL ctrl  = false,
         shift = false,
         alt   = false,
         super = false; // Cmd/Super/Windows
  };

  SHORT vKey        =   0;
  UINT  masked_code = 0x0; // For fast comparison

  void parse  (void);
  void update (void);

private:
  static void init (void);
};

// Adds a parameter to store and retrieve the keybind in an INI / XML file
struct SK_ConfigSerializedKeybind : public SK_Keybind
{
  SK_ConfigSerializedKeybind ( SK_Keybind&& bind,
                             const wchar_t* cfg_name) :
                               SK_Keybind  (bind)
  {
    if (cfg_name != nullptr)
    {
      wcsncpy_s ( short_name, 32,
                    cfg_name, _TRUNCATE );
    }
  }

  bool                  assigning       = false;
  wchar_t               short_name [32] = L"Uninitialized";
  wchar_t               param      [32] = L"Uninitialized"; //sk::ParameterStringW* param           = nullptr;
};

bool
SK_ImGui_KeybindSelect (SK_Keybind* keybind, const char* szLabel);

//SK_API
void
__stdcall
SK_ImGui_KeybindDialog (SK_Keybind* keybind);

bool
SK_ImGui_Keybinding (SK_Keybind* binding); // sk::ParameterStringW* param

#endif /* __SK__UTILITY_H__ */