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
//     A request replaces the one before it, if that is still loading. At most two images to show are decoded at once
//     (a still image cannot be stopped): a request beyond that waits until one is done.
//   SetNeighbours (shown, around): once nothing is loading, the viewer says what it shows and what is around it.
//     The shown image and the two next to it are kept (within a budget of video memory), and those two are preloaded,
//     one at a time, while nothing else is decoding. Animated images are neither preloaded nor kept: their frames take
//     much more memory.
//   Thumbnails (Thumbnail (path)): small, still versions (for the carousel), kept within a budget of their own, the least
//     recently used given up first. Those of the shown image and the ones nearest to it (SKIV_THUMBNAIL_REACH each way)
//     are made ahead; others once asked for. Each full decode makes one too, once its image is handed over. They are
//     made after the full preloads, a few at once (a JPEG's decoded at about the thumbnail's size).
//   A device reset (OnDeviceReset) releases them all, and drops the results made on the old device.
//
//   The viewer holds its own references to the images it shows (cover, and cover_old as it fades out).

// WM_SKIF_IMAGE's wParam when the store has a result to take: it only wakes the main loop (TakeResult takes it)
constexpr WPARAM SKIV_IMAGE_STORE_RESULT = 0x1;

// Thumbnails: scaled down to fit this many pixels each way. Made ahead: of the shown image and this many images each way
//   (the 5 nearest images)
constexpr size_t SKIV_THUMBNAIL_SIZE  = 256;
constexpr int    SKIV_THUMBNAIL_REACH = 2;

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

  // Main thread, once per frame while nothing is loading: the image shown (its path, empty if none) and the ones around it
  //   in its folder, nearest first: the next one, the previous one, the one after the next, ... (empty where there is
  //   none). Keeps the shown one and the two next to it, releases the rest, and preloads those two; then the thumbnails
  //   of the shown one and the rest of around. thumbnails: whether thumbnails are made at all (the carousel is used);
  //   once not, the kept ones are released.
  void SetNeighbours (const std::wstring& shown, const std::vector <std::wstring>& around, bool animatedImages,
                      bool thumbnails = true);

  // Main thread: the file's thumbnail (pRawTexSRV, its size, and what is known about it; no gamut coverage texture), or
  //   nullptr if there is none yet. load: made if it is not kept, after the thumbnails due ahead of it. Valid until the
  //   next call into the store; the textures are released after the frame they were last drawn in (SKIF_ResourcesToFree).
  const image_s* Thumbnail (const std::wstring& path, bool load = true);

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
    bool         thumbnail  = false;  // A thumbnail
    bool         byproduct  = false;  //   ... of a full decode (none if it was animated: not a failure)
    image_s      image = { };
  };

  // A decode in flight (main thread)
  struct inflight_s {
    int                                 job     = 0;
    key_s                               key;
    bool                                preload = false;
    bool                                thumbnail = false; // A thumbnail only
    std::shared_ptr <std::atomic<bool>> wanted;            // Read by its worker (an animated image stops once false)
    std::shared_ptr <void>              thread;            // Its worker thread (its handle, closed with the last copy)
  };

  // A kept image (main thread): the store's references
  struct entry_s {
    key_s   key;
    image_s image = { };
    size_t  bytes = 0;
  };

  // A kept thumbnail (main thread)
  struct thumb_s {
    key_s    key;
    image_s  image = { };
    size_t   bytes = 0;
    uint64_t used  = 0;     // When it was last asked for (m_tick)
  };

  static unsigned __stdcall Worker (void* job);
  static void Prioritize (const inflight_s& inflight, bool waitedFor); // Its worker's priority (see image_store.cpp)

  int    Start     (const std::wstring& path, const key_s& key, bool animatedImages, bool preload,  // Returns its job
                    bool thumbnailOnly = false);
  bool   IsWanted  (const key_s& key) const;               // Shown, next to it, or requested
  int    Priority  (const key_s& key) const;               // 3: shown or requested, 2: the next image, 1: the previous one, 0: not wanted
  bool   Keep      (const key_s& key, const image_s& image); // Into the cache (false if it does not fit the budget)
  entry_s*
         Find      (const key_s& key);
  void   Preload   (void);                                 // Starts one, if nothing is decoding and one is due
  void   StartDeferred (void);                             // Starts the request waiting for a decode to finish, if it can now
  thumb_s*
         FindThumbnail      (const key_s& key);
  bool   IsThumbnailWanted  (const key_s& key) const;      // Of the shown image or the ones nearest to it, or asked for
  bool   IsThumbnailComing  (const key_s& key) const;
  bool   KeepThumbnail      (const key_s& key, const image_s& image);
  bool   PreloadThumbnail   (void);                        // Starts one, if one is due (true if it did)
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
  bool         m_pendingDeferred = false;         //   ... it waits for a decode to finish (s_maxDecodes), m_pending then
                                                  //       being a number of its own
  bool         m_pendingWaited   = false;         //   ... it had to (logged once it starts)
  key_s        m_pendingKey;
  std::wstring m_pendingPath;
  bool         m_pendingAnimated = true;

  std::wstring m_shownPath;                       // SetNeighbours ( )
  std::vector <std::wstring>
               m_aroundPaths;
  key_s        m_shown,     m_prev,     m_next;
  std::vector <key_s>       m_near;               // The thumbnails made ahead: the shown image's, then around, nearest first
  bool         m_neighbourAnimated = true;
  bool         m_thumbnailsOn      = true;  // Thumbnails are made (SetNeighbours)

  std::vector <std::unique_ptr <thumb_s>>
                            m_thumbnails;         // (By pointer, as m_cache)
  std::vector <key_s>       m_thumbnailsAsked;    // Asked for (Thumbnail), not kept yet: the latest first
  std::vector <std::pair <int, key_s>>
                            m_thumbnailsComing;   // Being made, by their jobs (thumbnails only, and those of full decodes)
  std::vector <key_s>       m_thumbnailsFailed;   // Could not be made (until the neighbours change)
  uint64_t                  m_tick = 0;           // Counts the thumbnails asked for (least recently used)
};
