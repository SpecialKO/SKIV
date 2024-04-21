//
// Copyright 2020-2022 Andon "Kaldaien" Coleman
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



#define SKIV_MAJOR 0
#define SKIV_MINOR 0
#define SKIV_BUILD 0
#define SKIV_REV   1


#define _A2(a)     #a
#define  _A(a)  _A2(a)
#define _L2(w)  L ## w
#define  _L(w) _L2(w)


#if SKIV_REV > 0
#define SKIV_VERSION_STR_A    _A(SKIV_MAJOR) "." _A(SKIV_MINOR) "." _A(SKIV_BUILD) "." _A(SKIV_REV)
#else
#define SKIV_VERSION_STR_A    _A(SKIV_MAJOR) "." _A(SKIV_MINOR) "." _A(SKIV_BUILD)
#endif

#define SKIV_VERSION_STR_W _L(SKIV_VERSION_STR_A)


#define SKIV_FILE_VERSION     SKIV_MAJOR,SKIV_MINOR,SKIV_BUILD,SKIV_REV
#define SKIV_PRODUCT_VERSION  SKIV_MAJOR,SKIV_MINOR,SKIV_BUILD,SKIV_REV


#define SKIV_WINDOW_TITLE_A          "Special K Image Viewer"
#define SKIV_WINDOW_TITLE_W       _L("Special K Image Viewer")
#define SKIV_WINDOW_TITLE             SKIV_WINDOW_TITLE_W
#define SKIV_WINDOW_TITLE_SHORT_A    "SKIV"
#define SKIV_WINDOW_TITLE_SHORT_W _L("SKIV")
#define SKIV_WINDOW_TITLE_SHORT       SKIV_WINDOW_TITLE_SHORT_W
#define SKIV_WINDOW_HASH          "###Special K Image Viewer"

