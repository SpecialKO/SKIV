// How an image file describes its colors (color_source_s): read by src/utility/color_source.inl.
//   A header of its own, without Windows or DirectX types, so that tools\image_tests can use it as well.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
  uint16_t              exif_orientation = 0;       // Exif Orientation (not a color: read along with them): 0 if none, else
                                                    //   1 to 8 (1: upright; 6: to be turned 90 degrees clockwise...)

  bool empty (void) const
  {
    return icc.empty ( ) && ! has_cicp && ! has_srgb && gamma == 0 && ! has_chrm && exif_color_space == 0 && format_defined == nullptr;
  }
};
