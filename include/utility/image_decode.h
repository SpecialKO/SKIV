//
// Copyright 2024-2025 Andon "Kaldaien" Coleman
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

// Decoding images (src/utility/image_decode.cpp): a file, decoded into its texture(s) and what is known about it (image_s).
//   It runs on any thread: it reads no settings (they come with the request), and shows nothing (its notifications
//     come back with the result, to be shown on the main thread).

#include <atlbase.h>
#include <d3d11.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <initializer_list>
#include <imgui/imgui.h>
#include <ImGuiNotify.hpp>
#include <utility/utility.h> // FileSignature

// How the file describes its colors (ICC profile, cICP, ...): read by color_source.inl
struct color_source_s {
  const char*           origin           = nullptr; // What it was read from: "PNG", "JPEG", "WebP", "AVIF", "JPEG XL", "WIC"
  const char*           format_defined   = nullptr; // Formats whose colors are fixed by the format itself (OpenEXR, ...)

  std::vector <uint8_t> icc;                        // ICC profile (decompressed)
  std::string           icc_name;                   // Its description ('desc' tag)
  bool                  icc_has_cicp     = false;   // Its cicp tag (ICC 4.4), as HDR profiles have
  uint8_t               icc_primaries    = 0,
                        icc_transfer     = 0;

  bool                  has_cicp         = false;   // cICP (PNG), nclx (AVIF), or an encoded color space (JPEG XL)
  bool                  cicp_first       = false;   // cICP overrides the ICC profile (PNG: as the PNG specification says)
  uint8_t               primaries        = 0,
                        transfer         = 0,
                        matrix           = 0,
                        full_range       = 0;

  bool                  has_srgb         = false;   // PNG sRGB chunk
  uint8_t               rendering_intent = 0;

  uint32_t              gamma            = 0;       // PNG gAMA (the encoding gamma, times 100000; 45455 = 1 / 2.2), 0 if none
  bool                  has_chrm         = false;   // PNG cHRM: white point, red, green, blue (x, y each)
  float                 chrm [8]         = { };

  uint8_t               sbit [4]         = { };     // PNG sBIT: the significant bits of each channel, as the chunk has them
  uint8_t               sbit_count       = 0;       //   (1 to 4 bytes, by the color type: gray, gray + alpha, RGB, RGBA); 0 if none

  uint16_t              exif_color_space = 0;       // Exif ColorSpace: 0 if none, 1 sRGB, 2 Adobe RGB (WIC only), 65535 uncalibrated
  char                  exif_interop [4] = { };     // Exif InteroperabilityIndex: "R98" (sRGB), "R03" (Adobe RGB), "THM"

  bool empty (void) const
  {
    return icc.empty ( ) && ! has_cicp && ! has_srgb && gamma == 0 && ! has_chrm && exif_color_space == 0 && format_defined == nullptr;
  }
};

// The frames of an animated image (APNG, GIF, WebP, AVIF), and where it is in playing them (_LoadAnimatedImage).
//   A thread of its own decodes the frames from the file as they are played (image_anim_stream_s, image_decode.cpp),
//     each into a texture of its own, starting with the first: it shows as soon as that one is decoded. Those of an
//       animation that fits keepAll's budget are all kept once decoded (the thread then ends); those of a larger one are
//         streamed: only a few around the one shown are kept, and the file is decoded again each time it loops.
struct image_anim_stream_s;

// WM_SKIF_IMAGE's wParam when a frame the main thread waits for has been decoded: it only wakes the main loop
constexpr WPARAM SKIV_IMAGE_ANIMATION_FRAME = 0x2;

struct image_anim_s {
  ImVec2                resolution   = ImVec2 (0, 0);     // The frames' size (the image's)
  bool                  wide         = false;             // Of wide gamut colors: the frames are scRGB (half floats)
  bool                  streamed     = false;             // Only window frames are kept at a time (otherwise all of them)
  size_t                window       = 0;                 //   ... at most this many
  size_t                frameBytes   = 0;                 // The video memory a frame takes

  // Playback (main thread only)
  bool                  paused       = false;             // Paused by the user
  size_t                current      = 0;                 // The frame shown (or to be shown, once it is decoded)
  DWORD                 shownAt      = 0;                 // When it started to show (0: not yet)
  DWORD                 pausedIn     = 0;                 // Paused: how long it had shown (ms)
  CComPtr <ID3D11ShaderResourceView>
                        shown;                            // The texture shown: the current frame's, or the last one before it

  std::shared_ptr <image_anim_stream_s> stream;

  // Moves playback on to the frame due now (if it has been decoded), and returns the texture to show. nextAt: when the
  //   frame after it is due (SKIF_Util_timeGetTime1), 0 if none is (paused); changed: whether the texture changed.
  ID3D11ShaderResourceView*
         update          (DWORD now, DWORD& nextAt, bool& changed);
  void   play            (DWORD now);
  void   pause           (DWORD now);
  void   step            (DWORD now, int frames);         // Pauses, and moves this many frames ahead (or back)
  size_t getCount        (void) const;                    // How many frames it has
  DWORD  getDelay        (size_t frame) const;            // How long a frame shows (ms), 0 if not known yet (not decoded)
  DWORD  getDuration     (void) const;                    // ms, 0 until every frame has been decoded once
  void   release         (void);                          // Stops decoding, and pushes the textures to be released
                                                          //   (SKIF_ResourcesToFree): by whoever holds the last reference

  image_anim_s  (void) = default;
  image_anim_s  (const image_anim_s&) = delete;
  image_anim_s& operator= (const image_anim_s&) = delete;
 ~image_anim_s  (void);                                   // Stops decoding (if release did not)
};

struct image_s {
  struct file_s {
    std::wstring filename         = { }; // Image filename
    std:: string filename_utf8    = { };
    std::wstring folder_path      = { }; // Parent folder path
    std:: string folder_path_utf8 = { };
    std::wstring path             = { }; // Image path (full)
    std:: string path_utf8        = { };
    uint64_t     size             = 0;
    file_s ( ) { };
  } file_info;

  int          bpc         =    0;
  int          channels    =    0;
  float        width       = 0.0f;
  float        height      = 0.0f;
  ImVec2       uv0 = ImVec2 (0, 0);
  ImVec2       uv1 = ImVec2 (1, 1);
  ImVec2       avail_size;        // Holds the frame size used (affected by the scaling method)
  ImVec2       avail_size_cache;  // Holds a cached value used to determine if avail_size needs recalculating
  ImVec2       source_size = ImVec2 (0, 0); // A thumbnail: the size of the image it was made of (0 if unknown)

  bool         not_hdr     = false; //just an excessive bool for checks
  bool         is_hdr      = false;
  bool         is_wcg      = false; // SDR, of colors outside of sRGB: converted to scRGB (see color_convert.inl)
  bool         is_dds      = false;

  CComPtr <ID3D11ShaderResourceView>  pRawTexSRV;
  CComPtr <ID3D11UnorderedAccessView> pGamutCoverageUAV;
  CComPtr <ID3D11ShaderResourceView>  pGamutCoverageSRV;

  // Animated images: their frames (pRawTexSRV is then the first frame's texture, holding a reference of its own).
  //   Released with image_anim_s::release (SKIV_ImageStore::Release), like the textures above are pushed to SKIF_ResourcesToFree.
  std::shared_ptr <image_anim_s>      anim;

  // How the file describes its colors (ICC profile, cICP, ...), see color_source.inl
  color_source_s color_source;

  struct light_info_s {
    float max_cll      = 0.0f;
    char  max_cll_name =  '?';
    float max_nits     = 0.0f;
    float min_nits     = 0.0f;
    float avg_nits     = 0.0f;
    float p99_nits     = 0.0f;
    bool  isHDR        = false;
  } light_info;

  struct gamut_info_s {
    struct pixel_samples_s {
      uint32_t rec_709   = 0; // (Counted by the decode: they must start at 0)
      uint32_t rec_2020  = 0;
      uint32_t dci_p3    = 0;
      uint32_t ap1       = 0;
      uint32_t ap0       = 0;
      uint32_t undefined = 0;
      uint32_t total     = 0;

      float getPercentRec709    (void) const;
      float getPercentRec2020   (void) const;
      float getPercentDCIP3     (void) const;
      float getPercentAP1       (void) const;
      float getPercentAP0       (void) const;
      float getPercentUndefined (void) const;
    } pixel_counts;
  } colorimetry;

  // copy assignment
  image_s& operator= (const image_s other) noexcept
  {
    file_info           = other.file_info;
    bpc                 = other.bpc;
    channels            = other.channels;
    width               = other.width;
    height              = other.height;
    uv0                 = other.uv0;
    uv1                 = other.uv1;
    avail_size          = other.avail_size;
    avail_size_cache    = other.avail_size_cache;
    source_size         = other.source_size;
    pRawTexSRV.p        = other.pRawTexSRV.p;
    pGamutCoverageSRV.p = other.pGamutCoverageSRV.p;
    pGamutCoverageUAV.p = other.pGamutCoverageUAV.p;
    anim                = other.anim;
    color_source        = other.color_source;
    not_hdr             = other.not_hdr;
    is_hdr              = other.is_hdr;
    is_wcg              = other.is_wcg;
    is_dds              = other.is_dds;
    light_info          = other.light_info;
    colorimetry         = other.colorimetry;
    return *this;
  }

  void reset (void)
  {
    file_info           = { };
    bpc                 =    0;
    channels            =    0;
    width               = 0.0f;
    height              = 0.0f;
    uv0                 = ImVec2 (0, 0);
    uv1                 = ImVec2 (1, 1);
    avail_size          = { };
    avail_size_cache    = { };
    source_size         = { };
    pRawTexSRV.p        = nullptr;
    pGamutCoverageSRV.p = nullptr;
    pGamutCoverageUAV.p = nullptr;
    anim                = nullptr; // Not released: whoever was given it (cover_old) releases it
    color_source        = { };
    is_hdr              = false;
    is_wcg              = false;
    is_dds              = false;
    light_info          = { };
    colorimetry         = { };
  }
};

// The image formats SKIV reads, by their file signatures (and extensions)
extern const std::initializer_list <FileSignature> supported_formats;

bool isExtensionSupported  (const std::wstring extension);
bool isJXLDecoderAvailable (void);

// Whether the file is an animated image (APNG, GIF, WebP, or an AVIF image sequence), by its header
bool SKIV_IsAnimatedFile (const std::wstring& path);

struct SKIV_DecodeRequest {
  std::wstring                path;                  // The image file
  bool                        animatedImages = true; // Settings (bAnimatedImages), as they were when the request was made
  std::function <bool (void)> wanted;                // Optional: returns false once the image is no longer wanted (an
                                                     //   animated image then stops having its frames decoded)

  // Thumbnails: scaled down to fit this many pixels each way (never up), in the image's own format (HDR and wide gamut
  //   ones stay scRGB). No gamut coverage texture; their light levels are measured on the thumbnail.
  size_t                      thumbnailSize  = 0;     // 0: none
  bool                        thumbnailOnly  = false; // image is the thumbnail (still: an animated image's first frame,
                                                      //   as with animatedImages off)
  std::function <void (void)> decoded;                // Optional, with a thumbnail of a full decode: called once image is
                                                      //   ready, before the thumbnail is made (image is not touched again)

  // A preview of a large image (with thumbnailOnly, thumbnailSize its size): only if it can be made quickly, without
  //   decoding the image in full (a progressive JPEG's first scans); the decode fails otherwise
  bool                        previewOnly    = false;
};

// Decodes the file into image: its texture(s) (pRawTexSRV, the gamut coverage texture, an animated image's frames), what
//   is known about it (its size, format, colors and light levels), and its file_info (path, path_utf8, size).
//   notices: the notifications it has (to be shown on the main thread). Returns whether the file could be decoded.
//   thumbnail (with thumbnailSize, not thumbnailOnly): also made from the decoded image, if it is still (it is left
//     without a texture otherwise, or if it could not be made).
bool SKIV_DecodeImage (const SKIV_DecodeRequest& request, image_s& image, std::vector <ImGuiToast>& notices,
                       image_s* thumbnail = nullptr);

// How the file describes its colors, for the details panel: in a few words, and everything it has (one item per line)
std::string SKIV_DescribeColorSource         (const color_source_s& cs);
std::string SKIV_DescribeColorSourceInDetail (const color_source_s& cs);

// Saves a wide gamut SDR image (scRGB) in the colors of the file it was loaded from (cs), with its ICC profile embedded
//   (PNG, JPEG); otherwise as sRGB. Here, as it uses the same color code as the decoding.
namespace DirectX { struct Image; }
HRESULT SKIV_Image_SaveWideGamutSDR (const DirectX::Image& scrgb, const wchar_t* path, const color_source_s& cs);
