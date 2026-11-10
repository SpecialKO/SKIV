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

// The image store: loading images (decoding them on worker threads, SKIV_DecodeImage), keeping the ones next to the
//   shown one decoded (preloaded), and releasing their textures.
//
//   Main thread, except Release ( ), which any thread may use. Workers only produce results; TakeResult ( ) handles them
//   on the main thread (the viewer calls it once per frame): it shows the latest request's, keeps those of the images
//   shown and next to it, and releases the rest.
//
//   Request (path) -> from the cache, or a worker decodes it -> WM_SKIF_IMAGE wakes the main loop -> TakeResult (cover).
//   SetNeighbours (shown, previous, next): once nothing is loading, the viewer says what it shows and what is next to it.
//     Those three are kept (within a budget of video memory), and the two next to it are preloaded, one at a time, while
//     nothing else is decoding. Animated images are neither preloaded nor kept: their frames take much more memory.
//   A device reset (OnDeviceReset) releases them all, and drops the results made on the old device.
//
//   The viewer holds its own references to the images it shows (cover, and cover_old as it fades out).

// WM_SKIF_IMAGE's wParam when the store has a result to take: it only wakes the main loop (TakeResult takes it)
constexpr WPARAM SKIV_IMAGE_STORE_RESULT = 0x1;

#include <atomic>
#include <memory>
#include <string>
#include <vector>
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

  // The image to show next: from the cache, from the preload decoding it, or decoded on a worker thread.
  //   animatedImages: the setting (bAnimatedImages), as it is now.
  void Request    (const std::wstring& path, bool animatedImages);
  bool IsLoading  (void) const { return m_pending != 0; } // The latest request has not been taken yet

  // Main thread, once per frame: once the latest request is ready, puts it in image (its textures and what is known
  //   about it, as SKIV_DecodeImage gives them; anything image held is released first), and returns true. success:
  //   whether it could be decoded. Other results are kept (if they are wanted, see SetNeighbours) or released.
  bool TakeResult (image_s& image, bool& success);

  // Main thread, once per frame while nothing is loading: the image shown (its path, empty if none) and the ones next to
  //   it in its folder (empty if none). Keeps those three, releases the rest, and preloads the two next to it.
  void SetNeighbours (const std::wstring& shown, const std::wstring& previous, const std::wstring& next, bool animatedImages);

  // The notifications of the decodes (made on their worker threads), to be shown by the main thread
  bool TakeNotice (ImGuiToast& notice) { return m_notices.try_pop (notice); }

  // Pushes the image's textures (and an animated image's frames, once nothing else holds them) to be released after the
  //   frame (SKIF_ResourcesToFree), as they may still be drawn in it. Any thread. The image keeps what is known about it.
  //   Releases only image's own references: textures the store (or another image) holds stay.
  static void Release (image_s& image);

  // The device was reset: the images kept are released, and results still in flight (made on the old device) are too
  //   once they come. A request still loading is made again (true if there was one); the images shown are the caller's
  //   to load again.
  bool OnDeviceReset (void);

private:
  SKIV_ImageStore (void) = default;
  SKIV_ImageStore (SKIV_ImageStore const&) = delete;
  SKIV_ImageStore (SKIV_ImageStore&&)      = delete;

  // A file, as it is now: an edited file is another one
  struct key_s {
    std::wstring path;
    uint64_t     size    = 0;
    uint64_t     written = 0;
    bool         exists  = false;

    bool operator== (const key_s& other) const { return exists && other.exists && size == other.size &&
                                                        written == other.written && path == other.path; }
  };
  static key_s Key (const std::wstring& path);

  struct result_s {
    int          job        = 0;
    unsigned int generation = 0;
    key_s        key;
    bool         preload    = false;
    bool         success    = false;
    image_s      image = { };
  };

  // A decode in flight (main thread)
  struct inflight_s {
    int                                 job     = 0;
    key_s                               key;
    bool                                preload = false;
    std::shared_ptr <std::atomic<bool>> wanted;            // Read by its worker (an animated image stops once false)
  };

  // A kept image (main thread): the store's references
  struct entry_s {
    key_s   key;
    image_s image = { };
    size_t  bytes = 0;
  };

  static unsigned __stdcall Worker (void* job);

  int    Start     (const std::wstring& path, const key_s& key, bool animatedImages, bool preload); // Returns its job
  bool   IsWanted  (const key_s& key) const;               // Shown, next to it, or requested
  int    Priority  (const key_s& key) const;               // 3: shown or requested, 2: the next image, 1: the previous one, 0: not wanted
  bool   Keep      (const key_s& key, const image_s& image); // Into the cache (false if it does not fit the budget)
  entry_s*
         Find      (const key_s& key);
  void   Preload   (void);                                 // Starts one, if nothing is decoding and one is due
  static void   Copy  (const image_s& from, image_s& to);  // Its textures (references of to's own) and what is known about it
  static size_t Bytes (const image_s& image);               // What its textures take

  concurrency::concurrent_queue <result_s*>  m_results;
  concurrency::concurrent_queue <ImGuiToast> m_notices;
  std::atomic <unsigned int>                 m_generation { 0 }; // Bumped by a device reset (read by the workers)

  int                       m_jobs = 0;           // The last job handed out
  std::vector <inflight_s>  m_inflight;
  std::vector <std::unique_ptr <entry_s>>
                            m_cache;            // By pointer: image_s must not be copied or moved by the vector (its
                                                //   operator= hands over the texture pointers without references of their own)
  std::vector <key_s>       m_skip;               // Not to be preloaded (animated, failed, or too large) until the neighbours change

  int          m_pending         = 0;             // The request to show: the job decoding it (or a number of its own, from the cache)
  bool         m_pendingCached   = false;         //   ... it is in the cache
  key_s        m_pendingKey;
  std::wstring m_pendingPath;
  bool         m_pendingAnimated = true;

  std::wstring m_shownPath, m_prevPath, m_nextPath; // SetNeighbours ( )
  key_s        m_shown,     m_prev,     m_next;
  bool         m_neighbourAnimated = true;
};
