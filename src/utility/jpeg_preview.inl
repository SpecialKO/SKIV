//
// Copyright 2026 Andon "Kaldaien" Coleman
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

// Progressive JPEGs, quickly at an eighth of their size: their first scans hold each 8x8 block's DC coefficient (the
//   block's average), which is all that is decoded here. A large progressive JPEG takes seconds to decode in full (each
//     of its scans refines all of it); its first scans take a fraction of that, and make a fair preview (and thumbnail).
//       Included by image_decode.cpp.

#include <cstdint>
#include <vector>

// Whether the JPEG is progressive (by its frame header, SOF2): not one of SOF0/SOF1 (baseline, extended), nor one this
//   does not read (arithmetic coded, lossless, hierarchical)
static bool
_IsProgressiveJPEG (const uint8_t* data, size_t size)
{
  if (size < 4 || data [0] != 0xFF || data [1] != 0xD8)
    return false;

  size_t pos = 2;

  while (pos + 4 <= size)
  {
    if (data [pos] != 0xFF)
      return false;

    const uint8_t marker = data [pos + 1];

    // Fill bytes
    if (marker == 0xFF)
    {
      pos++;
      continue;
    }

    const size_t length = (static_cast <size_t> (data [pos + 2]) << 8) | data [pos + 3];

    if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC)
      return (marker == 0xC2);

    // The image data started without a frame header
    if (marker == 0xDA || marker == 0xD9 || length < 2)
      return false;

    pos += 2 + length;
  }

  return false;
}

// Decodes the DC coefficients of a progressive JPEG (its first scans) into an image of an eighth of its size (each pixel
//   a block's average), as RGBA (8 bpc, opaque): gray, YCbCr or RGB. CMYK and YCCK: if cmyk is set, as CMYK (4 bytes,
//     inverted as Adobe stores it; YCC made RGB) for the caller to convert (cmyk stays set), otherwise converted the simple
//       way, as stb_image does (cmyk is set back to false if the image is neither). width, height: its own size;
//         outWidth, outHeight: the image's. False if it is not a progressive JPEG of 8 bpc, or is malformed.
static bool
_DecodeJPEGDC (const uint8_t* data, size_t size, std::vector <uint8_t>& rgba, uint32_t& width, uint32_t& height,
                                                                            uint32_t& outWidth, uint32_t& outHeight, bool& cmyk)
{
  struct huffman_s {
    bool     defined     = false;
    uint8_t  fast  [512] = { }; // By the next 9 bits: the code's length (0: longer), ...
    uint8_t  fastValue [512] = { }; //   ... and its value
    int32_t  maxCode [18] = { };    // The largest code of each length (-1: none)
    int32_t  valPtr  [17] = { };    // The index (in values) of each length's first code, less that code
    uint8_t  values [256] = { };
  };

  struct component_s {
    uint8_t  id        = 0;
    int      h         = 1,
             v         = 1;
    int      tq        = 0;     // Its quantization table
    int      td        = 0;     // Its DC Huffman table (in the scan)
    int      pred      = 0;     // The last DC coefficient
    bool     done      = false; // Its DC coefficients are decoded
    std::vector <uint8_t> plane; // Each block's average (blocksWide x blocksHigh, MCU-padded)
  };

  if (size < 4 || data [0] != 0xFF || data [1] != 0xD8)
    return false;

  huffman_s   dc [4];
  uint16_t    quantDC [4]    = { 0, 0, 0, 0 }; // The DC entry of each quantization table
  bool        quantSet [4]   = { };
  component_s comps [4];
  int         count          = 0;   // How many components
  int         hMax           = 1,
              vMax           = 1;
  size_t      mcusX          = 0,
              mcusY          = 0;
  size_t      restart        = 0;   // The restart interval (MCUs), 0: none
  bool        frame          = false;
  bool        jfif           = false;
  int         adobeTransform = -1;  // APP14 "Adobe": 0: none (RGB, CMYK), 1: YCbCr, 2: YCCK; -1: no such segment

  auto _BuildHuffman = [](huffman_s& table, const uint8_t* counts, const uint8_t* symbols, size_t total) -> bool
  {
    table = { };

    if (total > 256)
      return false;

    memcpy (table.values, symbols, total);

    int32_t code  = 0;
    size_t  index = 0;

    for (int length = 1; length <= 16; length++)
    {
      const int n = counts [length - 1];

      table.valPtr  [length] = static_cast <int32_t> (index) - code;
      table.maxCode [length] = (n > 0) ? code + n - 1 : -1;

      // The short codes, by the next 9 bits
      for (int i = 0; i < n; i++, code++, index++)
      {
        if (length <= 9)
        {
          const int first = code << (9 - length),
                    last  = first + (1 << (9 - length));

          for (int k = first; k < last && k < 512; k++)
          {
            table.fast      [k] = static_cast <uint8_t> (length);
            table.fastValue [k] = symbols [index];
          }
        }
      }

      // (A code longer than its length allows: malformed)
      if (code > (1 << length))
        return false;

      code <<= 1;
    }

    table.maxCode [17] = INT32_MAX;
    table.defined      = true;
    return true;
  };

  // The entropy coded data of a scan: bits, with the stuffed zero after each 0xFF skipped, and zeros once a marker comes
  struct bits_s {
    const uint8_t* data;
    size_t         size;
    size_t         pos;
    uint32_t       buffer = 0;
    int            count  = 0;
    bool           marker = false; // A marker was reached

    void Fill (void)
    {
      while (count <= 24)
      {
        uint32_t byte = 0;

        if (! marker && pos < size)
        {
          byte = data [pos];

          if (byte == 0xFF)
          {
            const uint8_t next = (pos + 1 < size) ? data [pos + 1] : 0xD9;

            if (next == 0x00)
              pos += 2;
            else
            {
              marker = true;
              byte   = 0;
            }
          }

          else
            pos++;
        }

        buffer |= byte << (24 - count);
        count  += 8;
      }
    }

    uint32_t Peek (int n) { if (count < n) Fill ( ); return buffer >> (32 - n); }
    void     Skip (int n) { buffer <<= n; count -= n; }
    uint32_t Get  (int n) { const uint32_t v = Peek (n); Skip (n); return v; }

    // At a restart marker: from the byte after it, the bits before it dropped
    bool Restart (void)
    {
      buffer = 0;
      count  = 0;
      marker = false;

      // (The marker may still be ahead: after the bits left of the interval, and fill bytes)
      while (pos + 1 < size)
      {
        if (data [pos] == 0xFF && data [pos + 1] >= 0xD0 && data [pos + 1] <= 0xD7)
        {
          pos += 2;
          return true;
        }

        pos++;
      }

      return false;
    }
  };

  auto _Decode = [](bits_s& bits, const huffman_s& table) -> int
  {
    const uint32_t look = bits.Peek (9);

    if (const int length = table.fast [look]; length != 0)
    {
      bits.Skip (length);
      return table.fastValue [look];
    }

    const uint32_t code16 = bits.Peek (16);

    for (int length = 10; length <= 16; length++)
    {
      const int32_t code = static_cast <int32_t> (code16 >> (16 - length));

      if (code <= table.maxCode [length])
      {
        bits.Skip (length);

        const int32_t index = table.valPtr [length] + code;
        return (index >= 0 && index < 256) ? table.values [index] : -1;
      }
    }

    return -1; // Not a code: malformed
  };

  size_t pos = 2;

  while (pos + 4 <= size)
  {
    if (data [pos] != 0xFF)
      return false;

    const uint8_t marker = data [pos + 1];

    if (marker == 0xFF)
    {
      pos++;
      continue;
    }

    if (marker == 0xD9) // End of image
      break;

    const size_t length = (static_cast <size_t> (data [pos + 2]) << 8) | data [pos + 3];

    if (length < 2 || pos + 2 + length > size)
      return false;

    const uint8_t* seg    = data + pos + 4;
    const size_t   segLen = length - 2;

    switch (marker)
    {
      case 0xC2: // Progressive frame
      {
        if (frame || segLen < 6 || seg [0] != 8)
          return false;

        height = (static_cast <uint32_t> (seg [1]) << 8) | seg [2];
        width  = (static_cast <uint32_t> (seg [3]) << 8) | seg [4];
        count  = seg [5];

        if (width == 0 || height == 0 || (count != 1 && count != 3 && count != 4) || segLen < 6 + static_cast <size_t> (count) * 3)
          return false;

        for (int c = 0; c < count; c++)
        {
          comps [c].id = seg [6 + c * 3];
          comps [c].h  = seg [7 + c * 3] >> 4;
          comps [c].v  = seg [7 + c * 3] & 15;
          comps [c].tq = seg [8 + c * 3] & 3;

          if (comps [c].h < 1 || comps [c].h > 4 || comps [c].v < 1 || comps [c].v > 4)
            return false;

          hMax = std::max (hMax, comps [c].h);
          vMax = std::max (vMax, comps [c].v);
        }

        mcusX = (width  + 8 * hMax - 1) / (8 * hMax);
        mcusY = (height + 8 * vMax - 1) / (8 * vMax);

        for (int c = 0; c < count; c++)
          comps [c].plane.assign (mcusX * comps [c].h * mcusY * comps [c].v, 128);

        frame = true;
        break;
      }

      case 0xC0: case 0xC1: case 0xC3: case 0xC5: case 0xC6: case 0xC7:
      case 0xC9: case 0xCA: case 0xCB: case 0xCD: case 0xCE: case 0xCF:
        return false; // Not progressive (or not Huffman coded)

      case 0xC4: // Huffman tables
      {
        size_t i = 0;

        while (i + 17 <= segLen)
        {
          const int tc = seg [i] >> 4,
                    th = seg [i] & 15;

          size_t total = 0;

          for (int k = 0; k < 16; k++)
            total += seg [i + 1 + k];

          if (th > 3 || i + 17 + total > segLen)
            return false;

          if (tc == 0 && ! _BuildHuffman (dc [th], seg + i + 1, seg + i + 17, total))
            return false;

          i += 17 + total;
        }
        break;
      }

      case 0xDB: // Quantization tables (only their DC entries are needed)
      {
        size_t i = 0;

        while (i < segLen)
        {
          const int pq = seg [i] >> 4,
                    tq = seg [i] & 15;

          const size_t tableSize = (pq) ? 128 : 64;

          if (tq > 3 || i + 1 + tableSize > segLen)
            return false;

          quantDC  [tq] = (pq) ? static_cast <uint16_t> ((seg [i + 1] << 8) | seg [i + 2]) : seg [i + 1];
          quantSet [tq] = true;

          i += 1 + tableSize;
        }
        break;
      }

      case 0xDD: // Restart interval
        if (segLen >= 2)
          restart = (static_cast <size_t> (seg [0]) << 8) | seg [1];
        break;

      case 0xE0: // JFIF
        if (segLen >= 5 && memcmp (seg, "JFIF", 5) == 0)
          jfif = true;
        break;

      case 0xEE: // Adobe
        if (segLen >= 12 && memcmp (seg, "Adobe", 5) == 0)
          adobeTransform = seg [11];
        break;

      case 0xDA: // A scan
      {
        if (! frame || segLen < 1)
          return false;

        const int ns = seg [0];

        if (ns < 1 || ns > 4 || segLen < 1 + static_cast <size_t> (ns) * 2 + 3)
          return false;

        component_s* inScan [4] = { };

        for (int s = 0; s < ns; s++)
        {
          const uint8_t id = seg [1 + s * 2];

          for (int c = 0; c < count; c++)
            if (comps [c].id == id)
              inScan [s] = &comps [c];

          if (inScan [s] == nullptr)
            return false;

          inScan [s]->td = seg [2 + s * 2] >> 4;
        }

        const int ss = seg [1 + ns * 2],
                  se = seg [2 + ns * 2],
                  ah = seg [3 + ns * 2] >> 4,
                  al = seg [3 + ns * 2] & 15;

        // Its entropy coded data: up to the next marker other than a restart marker
        const size_t start = pos + 2 + length;
        size_t       end   = start;

        while (end + 1 < size && ! (data [end] == 0xFF && data [end + 1] != 0x00 && data [end + 1] != 0xFF &&
                                    (data [end + 1] < 0xD0 || data [end + 1] > 0xD7)))
          end++;

        // A first scan of DC coefficients: decoded
        if (ss == 0 && se == 0 && ah == 0)
        {
          for (int s = 0; s < ns; s++)
          {
            if (inScan [s]->td > 3 || ! dc [inScan [s]->td].defined || ! quantSet [inScan [s]->tq])
              return false;

            inScan [s]->pred = 0;
          }

          bits_s bits = { data, end, start };

          // Interleaved: MCUs of each component's blocks; one component: its blocks, row by row
          const bool   interleaved = (ns > 1);
          const size_t unitsX      = (interleaved) ? mcusX : (width  * inScan [0]->h + 8 * hMax - 1) / (8 * hMax),
                       unitsY      = (interleaved) ? mcusY : (height * inScan [0]->v + 8 * vMax - 1) / (8 * vMax);
          size_t       units       = 0;

          for (size_t uy = 0; uy < unitsY; uy++)
          {
            for (size_t ux = 0; ux < unitsX; ux++)
            {
              if (restart != 0 && units != 0 && units % restart == 0)
              {
                bits.Restart ( );

                for (int s = 0; s < ns; s++)
                  inScan [s]->pred = 0;
              }

              units++;

              for (int s = 0; s < ns; s++)
              {
                component_s& comp = *inScan [s];

                const int    bh     = (interleaved) ? comp.h : 1,
                             bv     = (interleaved) ? comp.v : 1;
                const size_t stride = mcusX * comp.h;

                for (int v = 0; v < bv; v++)
                {
                  for (int h = 0; h < bh; h++)
                  {
                    const int t = _Decode (bits, dc [comp.td]);

                    if (t < 0 || t > 16)
                      return false;

                    int diff = 0;

                    if (t > 0)
                    {
                      const int raw = static_cast <int> (bits.Get (t));
                      diff = (raw < (1 << (t - 1))) ? raw - (1 << t) + 1 : raw;
                    }

                    comp.pred += diff;

                    const size_t bx = (interleaved) ? ux * comp.h + h : ux,
                                 by = (interleaved) ? uy * comp.v + v : uy;

                    // Its average: the coefficient (times its quantizer) over 8, plus the level shift
                    const int value = ((comp.pred * (1 << al)) * static_cast <int> (quantDC [comp.tq])) / 8 + 128;

                    if (bx < stride && by * stride + bx < comp.plane.size ( ))
                      comp.plane [by * stride + bx] = static_cast <uint8_t> (std::clamp (value, 0, 255));
                  }
                }
              }
            }
          }

          for (int s = 0; s < ns; s++)
            inScan [s]->done = true;

          // All of them: done
          bool all = true;

          for (int c = 0; c < count; c++)
            all &= comps [c].done;

          if (all)
          {
            pos = size; // (Ends the loop)
            break;
          }
        }

        pos = end;
        continue;
      }

      default:
        break;
    }

    if (pos < size)
      pos += 2 + length;
  }

  if (! frame)
    return false;

  for (int c = 0; c < count; c++)
    if (! comps [c].done)
      return false;

  // An eighth of the image's size, each component sampled where its blocks are
  outWidth  = (width  + 7) / 8;
  outHeight = (height + 7) / 8;

  rgba.resize (static_cast <size_t> (outWidth) * outHeight * 4);

  if (! (count == 4 && (adobeTransform == 0 || adobeTransform == 2)))
    cmyk = false;

  // RGB: by its component IDs, or an Adobe segment saying it is not YCbCr (as stb_image takes them)
  const bool rgb = (count == 3) && ((comps [0].id == 'R' && comps [1].id == 'G' && comps [2].id == 'B') ||
                                    (adobeTransform == 0 && ! jfif));

  auto _Clamp = [](float v) -> uint8_t { return static_cast <uint8_t> (std::clamp (v + 0.5f, 0.0f, 255.0f)); };
  auto _Blinn = [](int x, int y) -> uint8_t { const int t = x * y + 128; return static_cast <uint8_t> ((t + (t >> 8)) >> 8); };

  for (uint32_t y = 0; y < outHeight; y++)
  {
    for (uint32_t x = 0; x < outWidth; x++)
    {
      uint8_t s [4] = { 0, 0, 0, 0 };

      for (int c = 0; c < count; c++)
      {
        const size_t stride = mcusX * comps [c].h,
                     bx     = static_cast <size_t> (x) * comps [c].h / hMax,
                     by     = static_cast <size_t> (y) * comps [c].v / vMax;

        s [c] = comps [c].plane [std::min (by * stride + bx, comps [c].plane.size ( ) - 1)];
      }

      uint8_t* out = &rgba [(static_cast <size_t> (y) * outWidth + x) * 4];

      if (count == 1)
        out [0] = out [1] = out [2] = s [0];

      else if (rgb || (count == 4 && adobeTransform == 0))
      {
        out [0] = s [0];
        out [1] = s [1];
        out [2] = s [2];
      }

      else
      {
        const float Y  = s [0],
                    Cb = s [1] - 128.0f,
                    Cr = s [2] - 128.0f;

        out [0] = _Clamp (Y + 1.402f    * Cr);
        out [1] = _Clamp (Y - 0.344136f * Cb - 0.714136f * Cr);
        out [2] = _Clamp (Y + 1.772f    * Cb);
      }

      // CMYK (Adobe stores it inverted), and YCCK: left as they are, for the caller to convert (cmyk); the simple way if not
      if (count == 4 && (adobeTransform == 0 || adobeTransform == 2))
      {
        if (cmyk)
        {
          // (YCC gives CMY as ink amounts: inverted, as K is)
          if (adobeTransform == 2)
            for (int c = 0; c < 3; c++)
              out [c] = 255 - out [c];

          out [3] = s [3];
          continue;
        }

        for (int c = 0; c < 3; c++)
          out [c] = _Blinn ((adobeTransform == 0) ? out [c] : 255 - out [c], s [3]);
      }

      out [3] = 255;
    }
  }

  return true;
}
