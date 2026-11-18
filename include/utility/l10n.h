#pragma once

// Localization (l10n) of the UI -- English only
//
//   SKIV shares a large part of its code with SKIF, where the text shown in the UI is wrapped in macros that
//     look up the translation of the text. SKIV is not translated, so this header gives the same macros without
//       the translation system (SKIF's utility/l10n.cpp, the .po files, and the language selection in the settings).
//         Keeping the macros allows the shared code to remain the same as in SKIF, which makes it easier to compare and merge.
//
//     _("text")                     Display text
//     C_("context", "text")         The same, with a context
//     L_("label")                   Widget labels (Button, MenuItem, Selectable, ...)
//     LI_(ICON_FA_..., "label")     L_ () with an icon in front
//     CL_("context", "label")       L_ () with a context
//     W_("text")                    Wide display text for Win32 (MessageBox, tray menu, ...)
//     CW_("context", "text")        W_ () with a context
//     N_("text")                    Marks text for translation without translating it (static arrays)
//     T_(variable)                  Translates text that is not a literal
//     NC_("context", "text")        N_ () with a context
//     CT_("context", variable)      T_ () with a context
//     NL_("label")                  N_ () for a widget label
//     TL_(variable)                 T_ () for a widget label
//     P_("singular", "plural", n)   Plural forms
//     F_("{0} of {1}", a, b)        Formats with std::vformat
//     FP_("{0} game", "{0} games", n, ...)   Plural forms + formatting; {0} is n, any further arguments are {1}, {2}, ...

#include <format>
#include <string>
#include <utility>

namespace SKIF_L10n
{
  // Whether a language tag is for Traditional Chinese (zh-Hant, zh-TW, zh-HK, zh-MO)
  inline bool
  IsTraditionalChinese (const std::string& tag)
  {
    return tag.find ("Hant") != std::string::npos || tag.find ("TW") != std::string::npos ||
           tag.find ("HK")   != std::string::npos || tag.find ("MO") != std::string::npos;
  }
}

// An icon in front of a label. The text is valid until the function has been called another 15 times, so for the widget it is passed to.
inline const char*
SKIF_L10n_IconLabel (const char* icon, const char* label)
{
  thread_local std::string buffers [16];
  thread_local unsigned    next = 0;

  std::string& buffer = buffers [next++ % 16];
  buffer = std::string (icon) + " " + label;

  return buffer.c_str ( );
}

#define _(text)                 (text)
#define C_(context, text)       (text)
#define L_(label)               (label)
#define LI_(icon, label)        SKIF_L10n_IconLabel (icon, label)
#define CL_(context, label)     (label)
#define W_(text)                (L"" text)
#define CW_(context, text)      (L"" text)
#define N_(text)                (text)
#define T_(text)                (text)
#define NC_(context, text)      (text)
#define CT_(context, text)      (text)
#define NL_(label)              (label)
#define TL_(label)              (label)
#define P_(singular, plural, n) (((n) == 1) ? (singular) : (plural))

template <class... Args>
inline std::string
SKIF_L10n_Format (const char* format, Args&&... args)
{
  return std::vformat (format, std::make_format_args (args...));
}

#define F_(format, ...)                  SKIF_L10n_Format (format, __VA_ARGS__)
#define FP_(singular, plural, n, ...)    SKIF_L10n_Format (P_ (singular, plural, n), n, ##__VA_ARGS__)
