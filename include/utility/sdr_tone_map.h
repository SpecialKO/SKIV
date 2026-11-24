#pragma once

// Adapted from JXRConverter's SKIV and auto tone curves.
// Copyright (c) 2026 Jingxuan Zhou
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <algorithm>
#include <cmath>

enum SKIV_SdrToneMapper
{
  SKIV_SDR_TONEMAP_DEFAULT = 0,
  SKIV_SDR_TONEMAP_PIECEWISE,
  SKIV_SDR_TONEMAP_AUTO
};

namespace SKIV_SdrTone
{
  // Input is scRGB (80 nits per unit). Output is linear SDR (one at white).
  inline constexpr float SDRWhite = 203.0f / 80.0f;
  inline constexpr float Shoulder = 0.5f * SDRWhite;
  inline constexpr float PQPeak   = 10000.0f / 80.0f;

  inline double ToPQ (double y)
  {
    const double p = std::pow (std::clamp (y / PQPeak, 0.0, 1.0), 0.1593017578125);
    return std::pow ((0.8359375 + 18.8515625 * p) / (1.0 + 18.6875 * p), 78.84375);
  }

  inline float FromPQ (float code)
  {
    const double p = std::pow (static_cast <double> (code), 1.0 / 78.84375);
    return static_cast <float> (PQPeak *
      std::pow (std::max (p - 0.8359375, 0.0) / (18.8515625 - 18.6875 * p), 1.0 / 0.1593017578125));
  }

  inline float Smooth (float lo, float hi, float value)
  {
    const float t = std::clamp ((value - lo) / (hi - lo), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
  }

  struct AutoCurve
  {
    float strength = 0.0f;
    float center   = 0.52f;
    float high     = 0.78f;

    float Gain (float luminance) const
    {
      if (strength == 0.0f)
        return 1.0f;

      const float p = static_cast <float> (ToPQ (luminance));
      const float weight = p < center ? Smooth (0.25f, center, p)
                                      : 1.0f - Smooth (center, high, p);
      return std::exp2 (-strength * weight);
    }
  };

  struct AutoStats
  {
    double visible = 0.0, sum_pq = 0.0, hdr_area = 0.0, excess_pq = 0.0;

    void Add (float luminance, float alpha)
    {
      if (! std::isfinite (alpha) || alpha <= 0.0f || ! std::isfinite (luminance))
        return;

      static const double shoulder_pq = ToPQ (Shoulder);
      const double p = luminance > 0.0f ? ToPQ (luminance) : 0.0;
      const double weight = std::min (alpha, 1.0f);
      visible += weight;
      sum_pq += weight * p;
      if (luminance > SDRWhite)
        hdr_area += weight;
      excess_pq += weight * std::max (p - shoulder_pq, 0.0);
    }

    AutoCurve Curve () const
    {
      if (visible == 0.0)
        return {};

      const float mean = static_cast <float> (sum_pq / visible);
      const float hdr_gate = Smooth (0.01f, 0.08f, static_cast <float> (hdr_area / visible));
      const float daylight_gate = Smooth (0.395f, 0.470f, mean);
      const float pressure = std::sqrt (std::clamp (static_cast <float> (excess_pq / visible) / 0.085f, 0.0f, 1.0f));
      const float strength = 1.20f * hdr_gate * daylight_gate * pressure;
      if (strength <= 1e-4f)
        return {};

      const float center = std::clamp (0.52f + 0.55f * (mean - 0.43f), 0.52f, 0.60f);
      return { strength, center, std::clamp (center + 0.26f, 0.78f, 0.86f) };
    }
  };

  struct ShoulderCurve
  {
    float knee_pq, peak_pq, offset, scale;

    static float Intensity (float input, float peak)
    {
      static const float white = static_cast <float> (ToPQ (1.5));
      return std::pow (input, 1.18f) * (1.0f + white / (peak * peak) * input) / (1.0f + input / white);
    }

    explicit ShoulderCurve (float peak)
    {
      static const float white_pq = static_cast <float> (ToPQ (SDRWhite));
      knee_pq = static_cast <float> (ToPQ (Shoulder));
      peak_pq = std::max (white_pq, static_cast <float> (ToPQ (peak)));
      offset  = Intensity (knee_pq, peak_pq);
      scale   = (white_pq - knee_pq) / (Intensity (peak_pq, peak_pq) - offset);
    }

    float Map (float input) const
    {
      return knee_pq + (Intensity (std::min (input, peak_pq), peak_pq) - offset) * scale;
    }
  };
}
