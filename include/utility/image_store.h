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

#include <windows.h>

// The image store: loading images (decoding them on worker threads, SKIV_DecodeImage) and releasing their textures.
//
//   Main thread, except Release ( ), which any thread may use. Workers only produce results; TakeResult ( ) applies the
//   latest request's on the main thread (the viewer calls it once per frame), and releases those that came too late.
//
//   Request (path) -> a worker decodes it -> WM_SKIF_IMAGE wakes the main loop -> TakeResult (cover) swaps it in.
//   A newer request makes the older ones stale: an animated image then stops having its frames decoded, and their
//   results are released instead of shown. So does a device reset (OnDeviceReset), for results made on the old device.
//
//   For now the viewer holds the images shown (cover, and cover_old as it fades out); this owns how they come and go.

// WM_SKIF_IMAGE's wParam when the store has a decoded image to take: it only wakes the main loop (TakeResult takes it)
constexpr WPARAM SKIV_IMAGE_STORE_RESULT = 0x1;

#include <atomic>
#include <string>
#include <concurrent_queue.h>
#include <utility/image_decode.h>

class SKIV_ImageStore
{
public:
  static SKIV_ImageStore& Get (void)
  {
    static SKIV_ImageStore instance;
    return instance;
  }

  // Starts decoding the file on a worker thread. animatedImages: the setting (bAnimatedImages), as it is now.
  void Request    (const std::wstring& path, bool animatedImages);
  bool IsLoading  (void) const { return m_pending != 0; } // The latest request has not been taken yet

  // Main thread, once per frame: once the latest request has been decoded, puts it in image (its textures and what is
  //   known about it, as SKIV_DecodeImage gives them; anything image held is released first), and returns true. success:
  //   whether it could be decoded. Results of older requests, or from before a device reset, are released instead.
  bool TakeResult (image_s& image, bool& success);

  // The notifications of the decodes (made on their worker threads), to be shown by the main thread
  bool TakeNotice (ImGuiToast& notice) { return m_notices.try_pop (notice); }

  // Pushes the image's textures (and an animated image's frames, once nothing else holds them) to be released after the
  //   frame (SKIF_ResourcesToFree), as they may still be drawn in it. Any thread. The image keeps what is known about it.
  static void Release (image_s& image);

  // The device was reset: results still in flight were made on the old one, so they are released instead of shown. A
  //   request still loading is made again (true if there was one); the images shown are the caller's to load again.
  bool OnDeviceReset (void);

private:
  SKIV_ImageStore (void) = default;
  SKIV_ImageStore (SKIV_ImageStore const&) = delete;
  SKIV_ImageStore (SKIV_ImageStore&&)      = delete;

  struct result_s {
    int          request    = 0;
    unsigned int generation = 0;
    bool         success    = false;
    image_s      image;
  };

  static unsigned __stdcall Worker (void* job);

  concurrency::concurrent_queue <result_s*>  m_results;
  concurrency::concurrent_queue <ImGuiToast> m_notices;
  std::atomic <int>                          m_latest     { 0 }; // The latest request (read by the workers)
  std::atomic <unsigned int>                 m_generation { 0 }; // Bumped by a device reset
  int                                        m_pending    = 0;   // The request whose result is awaited (0: none)
  std::wstring                               m_pendingPath;      //   ... and what it is for
  bool                                       m_pendingAnimated = true;
};
