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

void SKIF_UI_Tab_DrawViewer (void);

enum SKIV_HDR_Visualizations
{
  SKIV_HDR_VISUALIZTION_NONE    = 0,
  SKIV_HDR_VISUALIZTION_HEATMAP = 1,
  SKIV_HDR_VISUALIZTION_GAMUT   = 2,
  SKIV_HDR_VISUALIZTION_SDR     = 3
};

enum SKIV_HDR_VisualizationFlags
{
  SKIV_VIZ_FLAG_SDR_CONSIDER_LUMINANCE  = 0x1,
  SKIV_VIZ_FLAG_SDR_CONSIDER_GAMUT      = 0x2,
  SKIV_VIZ_FLAG_SDR_CONSIDER_OVERBRIGHT = 0x4
};

enum SKIV_HDR_TonemapType
{
  SKIV_TONEMAP_TYPE_NONE               = 0x0, // Let the display figure it out
  SKIV_TONEMAP_TYPE_CLIP               = 0x1, // Truncate the image before display
  SKIV_TONEMAP_TYPE_INFINITE_ROLLOFF   = 0x2, // Reduce to finite range (i.e. x/(1+x))
  SKIV_TONEMAP_TYPE_NORMALIZE_TO_CLL   = 0x4, // Content range mapped to [0,1]
  SKIV_TONEMAP_TYPE_MAP_CLL_TO_DISPLAY = 0x8  // Content range mapped to display range
};