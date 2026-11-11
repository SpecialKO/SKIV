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

#include <utility/image_store.h>

#include <SKIV.h>
#include <utility/utility.h>
#include <process.h>
#include <algorithm>
#include "DirectXTex/DirectXTex.h"

extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;

// What the kept images may take (the shown one, and the two next to it)
#ifdef _WIN64
static constexpr size_t s_cacheBudget = 512ull * 1024 * 1024;
#else
static constexpr size_t s_cacheBudget = 256ull * 1024 * 1024;
#endif

// What the kept thumbnails may take (a 128x128 one takes 85 KB with its mipmaps, or 171 KB as half floats)
static constexpr size_t s_thumbnailBudget = 64ull * 1024 * 1024;

// How many thumbnails asked for (Thumbnail) are remembered until they are made
static constexpr size_t s_thumbnailsAsked = 128;

// How many images to show may be decoded at once: a still image cannot be stopped once it is no longer wanted (e.g.
//   going through a folder quickly), and each takes a thread and memory of its own. Requests beyond that wait, and only
//   the latest of them is kept.
static constexpr std::ptrdiff_t s_maxDecodes = 2;

// How many thumbnails (alone, not those of full decodes) may be made at once. Each decodes its image (most at their full
//   size, a JPEG at about the thumbnail's), at a low priority.
#ifdef _WIN64
static constexpr std::ptrdiff_t s_maxThumbnailJobs = 3;
#else
static constexpr std::ptrdiff_t s_maxThumbnailJobs = 2;
#endif

namespace
{
  // A request, handed to its worker thread
  struct job_s {
    int                                 job        = 0;
    unsigned int                        generation = 0;
    bool                                preload    = false;
    SKIV_DecodeRequest                  decode;
    std::shared_ptr <std::atomic<bool>> wanted;
  };

  template <class T>
  void
  _PushForRelease (CComPtr <T>& ptr)
  {
    if (ptr.p != nullptr)
    {
      PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << ptr.p << " to be released";
      SKIF_ResourcesToFree.push (ptr.Detach ( ));
    }
  }

  // What the texture behind a view takes
  size_t
  _TextureBytes (ID3D11View* view)
  {
    if (view == nullptr)
      return 0;

    CComPtr <ID3D11Resource>  pResource;
    CComPtr <ID3D11Texture2D> pTexture;

    view->GetResource (&pResource.p);

    if (pResource.p == nullptr || FAILED (pResource->QueryInterface (IID_PPV_ARGS (&pTexture.p))))
      return 0;

    D3D11_TEXTURE2D_DESC desc = { };
    pTexture->GetDesc (&desc);

    const size_t bits  = DirectX::BitsPerPixel (desc.Format);
    size_t       bytes = 0;

    for (UINT mip = 0; mip < std::max (desc.MipLevels, 1u); mip++)
      bytes += (static_cast <size_t> (std::max (desc.Width  >> mip, 1u)) *
                static_cast <size_t> (std::max (desc.Height >> mip, 1u)) * bits + 7) / 8;

    return bytes * std::max (desc.ArraySize, 1u);
  }
}

void
SKIV_ImageStore::Release (image_s& image)
{
  _PushForRelease (image.pRawTexSRV);
  _PushForRelease (image.pGamutCoverageSRV);
  _PushForRelease (image.pGamutCoverageUAV);

  // An animated image's frames: the last reference pushes their textures (cover and cover_old may share them)
  if (image.anim != nullptr)
  {
    if (image.anim.use_count ( ) == 1)
    {
      for (auto& frame : image.anim->frames)
        if (frame.srv.p != nullptr)
          SKIF_ResourcesToFree.push (frame.srv.Detach ( ));

      PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushed the " << image.anim->frames.size ( ) << " frames of an animated image to be released";
    }

    image.anim.reset ( );
  }
}

// A worker's priority: above normal while its image is waited for (shown once decoded), below normal otherwise (preloads,
//   thumbnails, and decodes no longer wanted), so that they give way to it and to the main thread. Only the thread's own
//   priority: never the process's (efficiency mode sets that, see SKIV.cpp).
void
SKIV_ImageStore::Prioritize (const inflight_s& inflight, bool waitedFor)
{
  if (inflight.thread != nullptr)
    SetThreadPriority (inflight.thread.get ( ), (waitedFor) ? THREAD_PRIORITY_ABOVE_NORMAL : THREAD_PRIORITY_BELOW_NORMAL);
}

SKIV_ImageStore::key_s
SKIV_ImageStore::Key (const std::wstring& path)
{
  key_s key;
  key.path = path;

  WIN32_FILE_ATTRIBUTE_DATA data = { };

  if (! path.empty ( ) && GetFileAttributesExW (path.c_str ( ), GetFileExInfoStandard, &data))
  {
    key.exists  = true;
    key.size    = (static_cast <uint64_t> (data.nFileSizeHigh)             << 32) | data.nFileSizeLow;
    key.written = (static_cast <uint64_t> (data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
  }

  return key;
}

void
SKIV_ImageStore::Copy (const image_s& from, image_s& to)
{
  to.file_info         = from.file_info;
  to.bpc               = from.bpc;
  to.channels          = from.channels;
  to.width             = from.width;
  to.height            = from.height;
  to.uv0               = from.uv0;
  to.uv1               = from.uv1;
  to.source_size       = from.source_size;
  to.pRawTexSRV        = from.pRawTexSRV;        // (CComPtr: a reference of its own)
  to.pGamutCoverageSRV = from.pGamutCoverageSRV;
  to.pGamutCoverageUAV = from.pGamutCoverageUAV;
  to.light_info        = from.light_info;
  to.colorimetry       = from.colorimetry;
  to.is_hdr            = from.is_hdr;
  to.is_wcg            = from.is_wcg;
  to.is_dds            = from.is_dds;
  to.anim              = from.anim;
  to.color_source      = from.color_source;
}

size_t
SKIV_ImageStore::Bytes (const image_s& image)
{
  return _TextureBytes (image.pRawTexSRV.p) + _TextureBytes (image.pGamutCoverageSRV.p);
}

SKIV_ImageStore::entry_s*
SKIV_ImageStore::Find (const key_s& key)
{
  for (auto& entry : m_cache)
    if (entry->key == key)
      return entry.get ( );

  return nullptr;
}

bool
SKIV_ImageStore::IsWanted (const key_s& key) const
{
  return (key == m_shown || key == m_prev || key == m_next || (m_pending != 0 && key == m_pendingKey));
}

int
SKIV_ImageStore::Priority (const key_s& key) const
{
  if ((m_pending != 0 && key == m_pendingKey) || key == m_shown) return 3; // Shown, or to be shown
  if (key == m_next)                                               return 2;
  if (key == m_prev)                                               return 1;
                                                                   return 0; // Not wanted
}

bool
SKIV_ImageStore::Keep (const key_s& key, const image_s& image, const std::vector <ImGuiToast>& notices)
{
  if (Find (key) != nullptr)
    return true;

  const size_t bytes = Bytes (image);

  if (bytes == 0 || bytes > s_cacheBudget)
    return false;

  auto _Total = [&](void) -> size_t {
    size_t total = 0;
    for (const auto& entry : m_cache)
      total += entry->bytes;
    return total;
  };

  // Makes room, if it can: only from what matters less (not wanted, then the previous image, then the next one), so
  //   that two images next to the shown one that do not fit together never push each other out in turn
  const int priority = Priority (key);

  for (int victims = 0; victims < priority && _Total ( ) + bytes > s_cacheBudget; victims++)
  {
    size_t freeable = 0;

    for (const auto& entry : m_cache)
      if (Priority (entry->key) <= victims)
        freeable += entry->bytes;

    // (Not worth releasing anything if it would still not fit: either more may go, or it is not kept)
    if (_Total ( ) - freeable + bytes > s_cacheBudget)
    {
      if (victims + 1 < priority)
        continue;
      break;
    }

    for (auto it = m_cache.begin ( ); it != m_cache.end ( ) && _Total ( ) + bytes > s_cacheBudget; )
    {
      if (Priority ((*it)->key) <= victims)
      {
        PLOG_INFO << "[Image Store] Making room: no longer keeping " << SK_WideCharToUTF8 ((*it)->key.path);
        Release ((*it)->image);
        it = m_cache.erase (it);
      }
      else
        ++it;
    }
  }

  if (_Total ( ) + bytes > s_cacheBudget)
  {
    PLOG_INFO << "[Image Store] Not keeping " << SK_WideCharToUTF8 (key.path) << " (" << (bytes / (1024 * 1024))
              << " MB): it does not fit in " << (s_cacheBudget / (1024 * 1024)) << " MB with the images that matter more";
    return false;
  }

  auto entry = std::make_unique <entry_s> ( );
  entry->key   = key;
  entry->bytes   = bytes;
  entry->notices = notices; // (Shown with it, whenever it is)
  Copy (image, entry->image);

  m_cache.push_back (std::move (entry));

  PLOG_INFO << "[Image Store] Keeping " << SK_WideCharToUTF8 (key.path) << " (" << (bytes / (1024 * 1024)) << " MB; "
            << (_Total ( ) / (1024 * 1024)) << " MB kept)";
  return true;
}

int
SKIV_ImageStore::Start (const std::wstring& path, const key_s& key, bool animatedImages, bool preload, bool thumbnailOnly)
{
  job_s* job = new job_s;

  job->job                   = ++m_jobs;
  job->generation            = m_generation.load ( );
  job->preload               = preload;
  job->decode.path           = path;
  job->decode.animatedImages = animatedImages && ! thumbnailOnly;
  job->wanted                = std::make_shared <std::atomic<bool>> (true);

  // A thumbnail: only one, or one made from a full decode, unless there is one (or one is being made)
  if (thumbnailOnly || (m_thumbnailsOn && key.exists && FindThumbnail (key) == nullptr && ! IsThumbnailComing (key)))
  {
    job->decode.thumbnailSize = SKIV_THUMBNAIL_SIZE;
    job->decode.thumbnailOnly = thumbnailOnly;

    m_thumbnailsComing.push_back ({ job->job, key });
  }

  // Animated images are stopped between frames once they are no longer wanted
  std::shared_ptr <std::atomic<bool>> wanted = job->wanted;
  job->decode.wanted = [wanted](void) -> bool { return wanted->load ( ); };

  if (thumbnailOnly)
    PLOG_INFO << "[Image Store] Making a thumbnail of " << SK_WideCharToUTF8 (path);
  else if (preload)
    PLOG_INFO << "[Image Store] Preloading " << SK_WideCharToUTF8 (path);

  const int id = job->job;

  // Started once its priority is set (Prioritize)
  HANDLE hWorkerThread = reinterpret_cast <HANDLE> (
    _beginthreadex (nullptr, 0x0, &SKIV_ImageStore::Worker, job, CREATE_SUSPENDED, nullptr)
  );

  if (hWorkerThread != NULL)
  {
    inflight_s inflight;
    inflight.job       = id;
    inflight.key       = key;
    inflight.preload   = preload;
    inflight.thumbnail = thumbnailOnly;
    inflight.wanted    = job->wanted;
    inflight.thread    = std::shared_ptr <void> (hWorkerThread, &CloseHandle);

    Prioritize (inflight, ! preload);
    m_inflight.push_back (inflight);

    ResumeThread (hWorkerThread);
  }

  else
  {
    // No worker: the request has failed (TakeResult reports it)
    PLOG_ERROR << "Failed to start an image worker!";

    result_s* result = new result_s;

    result->job        = job->job;
    result->generation = job->generation;
    result->key        = key;
    result->preload    = preload;
    result->thumbnail  = thumbnailOnly;

    m_results.push (result);

    // ... and so has its thumbnail
    if (job->decode.thumbnailSize > 0 && ! thumbnailOnly)
    {
      result_s* thumbnail = new result_s;

      thumbnail->job        = job->job;
      thumbnail->generation = job->generation;
      thumbnail->key        = key;
      thumbnail->preload    = preload;
      thumbnail->thumbnail  = true;
      thumbnail->byproduct  = true;

      m_results.push (thumbnail);
    }

    delete job;
  }

  return id;
}

void
SKIV_ImageStore::Request (const std::wstring& path, bool animatedImages)
{
  const key_s key = Key (path);

  m_pendingKey      = key;
  m_pendingPath     = path;
  m_pendingAnimated = animatedImages;
  m_pendingCached   = false;
  m_pendingDeferred = false;
  m_pendingWaited   = false;

  // Decodes in flight for other files are no longer wanted (their results are still kept, if they turn out to be)
  inflight_s* decoding = nullptr;

  for (auto& inflight : m_inflight)
  {
    if (inflight.thumbnail)
      continue;

    if (inflight.key == key)
      decoding = &inflight;

    // (Which also gives way to the decode of the image now wanted)
    else
    {
      inflight.wanted->store (false);
      Prioritize (inflight, false);
    }
  }

  // From the cache: taken (shown) in the next frame
  if (Find (key) != nullptr)
  {
    PLOG_INFO << "[Image Store] Showing " << SK_WideCharToUTF8 (path) << " (kept)";

    m_pending       = ++m_jobs;
    m_pendingCached = true;

    PostMessage (SKIF_Notify_hWnd, WM_SKIF_IMAGE, SKIV_IMAGE_STORE_RESULT, 0);
  }

  // From its preload, still decoding
  else if (decoding != nullptr)
  {
    PLOG_INFO << "[Image Store] Showing " << SK_WideCharToUTF8 (path) << " (being preloaded)";

    decoding->wanted->store (true);
    decoding->preload = false;
    Prioritize (*decoding, true);
    m_pending         = decoding->job;
  }

  // Decoded: now, unless too many images to show are being decoded already (it then starts once one is done)
  else
  {
    m_pending         = ++m_jobs; // (Its job, once it starts)
    m_pendingDeferred = true;

    StartDeferred ( );
  }
}

void
SKIV_ImageStore::StartDeferred (void)
{
  if (! m_pendingDeferred || m_pending == 0)
    return;

  // Decodes of images to show (including those no longer wanted: a still image cannot be stopped)
  const auto decoding =
    std::count_if (m_inflight.begin ( ), m_inflight.end ( ), [](const inflight_s& inflight) {
      return ! inflight.preload && ! inflight.thumbnail; });

  if (decoding >= s_maxDecodes)
  {
    m_pendingWaited = true;
    return;
  }

  const bool waited = std::exchange (m_pendingWaited, false);
  m_pendingDeferred = false;

  // Kept meanwhile (preloaded), or being preloaded
  if (Find (m_pendingKey) != nullptr)
  {
    PLOG_INFO << "[Image Store] Showing " << SK_WideCharToUTF8 (m_pendingPath) << " (kept)";

    m_pendingCached = true;
    PostMessage (SKIF_Notify_hWnd, WM_SKIF_IMAGE, SKIV_IMAGE_STORE_RESULT, 0);
    return;
  }

  for (auto& inflight : m_inflight)
  {
    if (! inflight.thumbnail && inflight.key == m_pendingKey)
    {
      PLOG_INFO << "[Image Store] Showing " << SK_WideCharToUTF8 (m_pendingPath) << " (being preloaded)";

      inflight.wanted->store (true);
      inflight.preload = false;
      Prioritize (inflight, true);
      m_pending        = inflight.job;
      return;
    }
  }

  PLOG_INFO_IF (waited) << "[Image Store] Loading " << SK_WideCharToUTF8 (m_pendingPath) << " (once an image no longer wanted was decoded)";

  m_pending = Start (m_pendingPath, m_pendingKey, m_pendingAnimated, false);
}

unsigned __stdcall
SKIV_ImageStore::Worker (void* var)
{
  SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIV_ImageWorker");

  job_s*           job   = static_cast <job_s*> (var);
  SKIV_ImageStore& store = SKIV_ImageStore::Get ( );

  // (Its priority was set by Start, which started it: see Prioritize)

  CoInitializeEx (nullptr, 0x0);

  PLOG_DEBUG << "SKIV_ImageWorker thread started!";

  PLOG_INFO  << "Streaming game cover asynchronously...";

  result_s* result = new result_s;

  result->job        = job->job;
  result->generation = job->generation;
  result->key        = Key (job->decode.path);
  result->preload    = job->preload;
  result->thumbnail  = job->decode.thumbnailOnly;

  // A full decode's thumbnail: a result of its own, made once the image is handed over
  result_s* thumbnail = nullptr;

  if (job->decode.thumbnailSize > 0 && ! job->decode.thumbnailOnly)
  {
    thumbnail = new result_s;

    thumbnail->job        = result->job;
    thumbnail->generation = result->generation;
    thumbnail->key        = result->key;
    thumbnail->preload    = result->preload;
    thumbnail->thumbnail  = true;
    thumbnail->byproduct  = true;
  }

  std::vector <ImGuiToast> notices;

  // The main thread decides what becomes of it (TakeResult), and wakes up (and refreshes the window) to take it. Its
  //   notices go with it: shown only if it is (a thumbnail's never are).
  auto _Hand = [&](result_s* handed)
  {
    if (! handed->thumbnail)
      handed->notices.insert (handed->notices.end ( ), notices.begin ( ), notices.end ( ));

    notices.clear ( );

    store.m_results.push (handed);
    PostMessage (SKIF_Notify_hWnd, WM_SKIF_IMAGE, SKIV_IMAGE_STORE_RESULT, 0);
  };

  bool handed = false;

  if (thumbnail != nullptr)
    job->decode.decoded = [&](void) { result->success = true; _Hand (result); handed = true; };

  const bool success =
    SKIV_DecodeImage (job->decode, result->image, notices, (thumbnail != nullptr) ? &thumbnail->image : nullptr);

  if (! handed)
  {
    result->success = success;
    _Hand (result);
  }

  if (thumbnail != nullptr)
  {
    thumbnail->success = (thumbnail->image.pRawTexSRV.p != nullptr);
    _Hand (thumbnail);
  }

  delete job;

  PLOG_INFO  << "Finished streaming image asynchronously...";
  PLOG_DEBUG << "SKIV_ImageWorker thread stopped!";

  return 0;
}

bool
SKIV_ImageStore::OnDeviceReset (void)
{
  m_generation.fetch_add (1);

  // The kept images were made on the old device, and so are the results in flight (released once they come)
  for (auto& entry : m_cache)
    Release (entry->image);

  m_cache.clear ( );

  for (auto& inflight : m_inflight)
    inflight.wanted->store (false);

  m_inflight.clear ( );
  m_skip.clear ( );

  for (auto& thumbnail : m_thumbnails)
    Release (thumbnail->image);

  m_thumbnails.clear ( );
  m_thumbnailsComing.clear ( );
  m_thumbnailsFailed.clear ( );

  if (m_pending == 0)
    return false;

  const std::wstring path = m_pendingPath; // (Request ( ) sets it again)
  Request (path, m_pendingAnimated);

  return true;
}

bool
SKIV_ImageStore::TakeResult (image_s& image, bool& success)
{
  bool taken = false;

  auto _Show = [&](const image_s& from, bool decoded, const std::vector <ImGuiToast>& notices)
  {
    Release (image);
    Copy    (from, image);

    // What its decode had to say (e.g. colors the file left unspecified), now that it is shown
    for (const auto& notice : notices)
      m_notices.push (notice);

    // The gamut coverage chart starts out empty (the renderer clears it each frame once it draws into it). Here, on
    //   the main thread, as the immediate context is not thread-safe (the worker could not clear it)
    if (image.pGamutCoverageUAV.p != nullptr)
    {
      CComPtr <ID3D11Device>        pDevice;
      CComPtr <ID3D11DeviceContext> pDevCtx;

      image.pGamutCoverageUAV->GetDevice (&pDevice.p);

      if (pDevice.p != nullptr)
        pDevice->GetImmediateContext (&pDevCtx.p);

      if (pDevCtx.p != nullptr)
      {
        const FLOAT fClearColor [] = { 0.f, 0.f, 0.f, 0.f };
        pDevCtx->ClearUnorderedAccessViewFloat (image.pGamutCoverageUAV, fClearColor);
      }
    }

    success   = decoded;
    m_pending = 0;
    taken     = true;
  };

  result_s* result = nullptr;

  while (m_results.try_pop (result))
  {
    // The textures its worker made: D3D11 keeps their initial data until the device next works, which the main loop
    //   sees to after this frame (see SKIV.cpp, where it releases resources)
    extern bool SKIV_DeviceCleanupWanted;
    SKIV_DeviceCleanupWanted = true;

    // A thumbnail: kept (if it fits), or remembered as one that cannot be made
    if (result->thumbnail)
    {
      m_thumbnailsComing.erase (std::remove_if (m_thumbnailsComing.begin ( ), m_thumbnailsComing.end ( ),
                                  [&](const auto& coming) { return coming.first == result->job; }), m_thumbnailsComing.end ( ));

      if (! result->byproduct)
        m_inflight.erase (std::remove_if (m_inflight.begin ( ), m_inflight.end ( ),
                            [&](const inflight_s& inflight) { return inflight.job == result->job; }), m_inflight.end ( ));

      if (result->generation == m_generation.load ( ))
      {
        if (result->success)
          KeepThumbnail (result->key, result->image);

        // (None from a full decode of an animated image: it is made of its first frame instead)
        else if (! result->byproduct)
          m_thumbnailsFailed.push_back (result->key);
      }

      Release (result->image);
      delete result;
      continue;
    }

    m_inflight.erase (std::remove_if (m_inflight.begin ( ), m_inflight.end ( ),
                        [&](const inflight_s& inflight) { return inflight.job == result->job; }), m_inflight.end ( ));

    // Made on the current device
    if (result->generation == m_generation.load ( ))
    {
      const bool show = (m_pending != 0 && ! m_pendingCached && result->job == m_pending);

      // Kept, if it is wanted (animated images are not: their frames take much more memory)
      if (result->success && result->image.anim == nullptr && (show || IsWanted (result->key)))
      {
        if (! Keep (result->key, result->image, result->notices) && result->preload)
          m_skip.push_back (result->key);
      }

      else if (result->preload)
        m_skip.push_back (result->key);

      if (show)
        _Show (result->image, result->success, result->notices);
    }

    Release (result->image);
    delete result;
  }

  // From the cache
  if (m_pending != 0 && m_pendingCached)
  {
    if (entry_s* entry = Find (m_pendingKey))
      _Show (entry->image, true, entry->notices);

    // (No longer kept: decoded after all)
    else
    {
      m_pendingCached   = false;
      m_pendingDeferred = true;
    }
  }

  // A request waiting for a decode to finish: started, if one has
  StartDeferred ( );

  return taken;
}

void
SKIV_ImageStore::SetNeighbours (const std::wstring& shown, const std::vector <std::wstring>& around, bool animatedImages, bool thumbnails)
{
  m_neighbourAnimated = animatedImages;

  // No thumbnails (the carousel is not used): those kept are released
  if (thumbnails != m_thumbnailsOn)
  {
    m_thumbnailsOn = thumbnails;

    if (! thumbnails)
    {
      PLOG_INFO_IF (! m_thumbnails.empty ( )) << "[Image Store] No longer keeping the " << m_thumbnails.size ( ) << " thumbnails: they are not used";

      for (auto& thumbnail : m_thumbnails)
        Release (thumbnail->image);

      m_thumbnails.clear ( );
      m_thumbnailsAsked.clear ( );
    }

    m_aroundPaths.clear ( ); // (Takes the neighbours in again below, with or without the thumbnails made ahead)
    m_shownPath = L"\1";
  }

  if (shown != m_shownPath || around != m_aroundPaths)
  {
    m_shownPath   = shown;
    m_aroundPaths = around;

    m_shown = Key (shown);
    m_next  = Key ((around.size ( ) > 0) ? around [0] : L"");
    m_prev  = Key ((around.size ( ) > 1) ? around [1] : L"");

    m_near.clear ( );

    if (m_thumbnailsOn)
    {
      m_near.push_back (m_shown);

      for (size_t i = 0; i < around.size ( ); i++)
        m_near.push_back ((i == 0) ? m_next : (i == 1) ? m_prev : Key (around [i]));
    }

    m_skip.clear ( );
    m_thumbnailsFailed.clear ( );

    // Thumbnails of files changed since they were made
    for (auto it = m_thumbnails.begin ( ); it != m_thumbnails.end ( ); )
    {
      const bool changed =
        std::any_of (m_near.begin ( ), m_near.end ( ), [&](const key_s& key) {
          return key.exists && key.path == (*it)->key.path && ! (key == (*it)->key); });

      if (changed)
      {
        PLOG_INFO << "[Image Store] No longer keeping the thumbnail of " << SK_WideCharToUTF8 ((*it)->key.path) << ": the file has changed";
        Release ((*it)->image);
        it = m_thumbnails.erase (it);
      }
      else
        ++it;
    }

    // What is no longer wanted: released (the viewer keeps its own references to what it shows)
    for (auto it = m_cache.begin ( ); it != m_cache.end ( ); )
    {
      if (! IsWanted ((*it)->key))
      {
        PLOG_INFO << "[Image Store] No longer keeping " << SK_WideCharToUTF8 ((*it)->key.path);
        Release ((*it)->image);
        it = m_cache.erase (it);
      }
      else
        ++it;
    }

    // Preloads no longer wanted (an animated image would stop; a still one is released once decoded)
    for (auto& inflight : m_inflight)
      if (inflight.preload && ! IsWanted (inflight.key))
        inflight.wanted->store (false);
  }

  Preload ( );
}

void
SKIV_ImageStore::Preload (void)
{
  // One at a time, and only while no other image is decoding (thumbnails may be made meanwhile)
  if (m_pending != 0 || std::any_of (m_inflight.begin ( ), m_inflight.end ( ), [](const inflight_s& inflight) { return ! inflight.thumbnail; }))
    return;

  for (const key_s* candidate : { &m_next, &m_prev })
  {
    const key_s& key = *candidate;

    if (! key.exists || key == m_shown || Find (key) != nullptr ||
        std::find (m_skip.begin ( ), m_skip.end ( ), key) != m_skip.end ( ))
      continue;

    // Animated images are not kept: their frames take much more memory
    if (SKIV_IsAnimatedFile (key.path))
    {
      m_skip.push_back (key);
      continue;
    }

    Start (key.path, key, m_neighbourAnimated, true);
    return;
  }

  // The full images are kept: the thumbnails next
  PreloadThumbnail ( );
}

SKIV_ImageStore::thumb_s*
SKIV_ImageStore::FindThumbnail (const key_s& key)
{
  for (auto& thumbnail : m_thumbnails)
    if (thumbnail->key == key)
      return thumbnail.get ( );

  return nullptr;
}

bool
SKIV_ImageStore::IsThumbnailWanted (const key_s& key) const
{
  return (std::find (m_near.begin ( ), m_near.end ( ), key) != m_near.end ( ) || (m_pending != 0 && key == m_pendingKey));
}

bool
SKIV_ImageStore::IsThumbnailComing (const key_s& key) const
{
  return std::any_of (m_thumbnailsComing.begin ( ), m_thumbnailsComing.end ( ), [&](const auto& coming) { return coming.second == key; });
}

bool
SKIV_ImageStore::KeepThumbnail (const key_s& key, const image_s& image)
{
  if (FindThumbnail (key) != nullptr)
    return true;

  const size_t bytes = Bytes (image);

  if (bytes == 0 || bytes > s_thumbnailBudget)
    return false;

  auto _Total = [&](void) -> size_t {
    size_t total = 0;
    for (const auto& thumbnail : m_thumbnails)
      total += thumbnail->bytes;
    return total;
  };

  // Makes room: the least recently used thumbnails first, never those made ahead
  while (_Total ( ) + bytes > s_thumbnailBudget)
  {
    auto victim = m_thumbnails.end ( );

    for (auto it = m_thumbnails.begin ( ); it != m_thumbnails.end ( ); ++it)
      if (! IsThumbnailWanted ((*it)->key) && (victim == m_thumbnails.end ( ) || (*it)->used < (*victim)->used))
        victim = it;

    if (victim == m_thumbnails.end ( ))
      break;

    PLOG_VERBOSE << "[Image Store] Making room: no longer keeping the thumbnail of " << SK_WideCharToUTF8 ((*victim)->key.path);
    Release ((*victim)->image);
    m_thumbnails.erase (victim);
  }

  if (_Total ( ) + bytes > s_thumbnailBudget)
  {
    PLOG_INFO << "[Image Store] Not keeping the thumbnail of " << SK_WideCharToUTF8 (key.path) << ": it does not fit in "
              << (s_thumbnailBudget / (1024 * 1024)) << " MB with those of the images nearest to the shown one";
    return false;
  }

  auto thumbnail = std::make_unique <thumb_s> ( );
  thumbnail->key   = key;
  thumbnail->bytes = bytes;
  thumbnail->used  = ++m_tick;
  Copy (image, thumbnail->image);

  m_thumbnails.push_back (std::move (thumbnail));

  m_thumbnailsAsked.erase (std::remove (m_thumbnailsAsked.begin ( ), m_thumbnailsAsked.end ( ), key), m_thumbnailsAsked.end ( ));

  PLOG_INFO << "[Image Store] Keeping a thumbnail of " << SK_WideCharToUTF8 (key.path) << " ("
            << static_cast <int> (image.width) << "x" << static_cast <int> (image.height) << ", " << (bytes / 1024) << " KB; "
            << m_thumbnails.size ( ) << " thumbnails, " << (_Total ( ) / 1024) << " KB kept)";
  return true;
}

bool
SKIV_ImageStore::PreloadThumbnail (void)
{
  if (! m_thumbnailsOn || m_pending != 0)
    return false;

  // A few at once (s_maxThumbnailJobs), each on a thread of its own
  auto running =
    std::count_if (m_inflight.begin ( ), m_inflight.end ( ), [](const inflight_s& inflight) { return inflight.thumbnail; });

  auto _Due = [&](const key_s& key) -> bool {
    return key.exists && FindThumbnail (key) == nullptr && ! IsThumbnailComing (key) &&
           std::find (m_thumbnailsFailed.begin ( ), m_thumbnailsFailed.end ( ), key) == m_thumbnailsFailed.end ( );
  };

  bool started = false;

  // The shown image's and those nearest to it, then those asked for (the latest first)
  for (const auto* keys : { &m_near, &m_thumbnailsAsked })
  {
    for (const key_s& key : *keys)
    {
      if (running >= s_maxThumbnailJobs)
        return started;

      if (_Due (key))
      {
        Start (key.path, key, false, true, true);
        running++;
        started = true;
      }
    }
  }

  return started;
}

const image_s*
SKIV_ImageStore::Thumbnail (const std::wstring& path, bool load)
{
  if (path.empty ( ))
    return nullptr;

  for (auto& thumbnail : m_thumbnails)
  {
    if (thumbnail->key.path == path)
    {
      thumbnail->used = ++m_tick;
      return &thumbnail->image;
    }
  }

  if (load && m_thumbnailsOn)
  {
    auto asked = std::find_if (m_thumbnailsAsked.begin ( ), m_thumbnailsAsked.end ( ), [&](const key_s& key) { return key.path == path; });

    // The latest first
    if (asked != m_thumbnailsAsked.end ( ))
      std::rotate (m_thumbnailsAsked.begin ( ), asked, asked + 1);

    else
    {
      m_thumbnailsAsked.insert (m_thumbnailsAsked.begin ( ), Key (path));

      if (m_thumbnailsAsked.size ( ) > s_thumbnailsAsked)
        m_thumbnailsAsked.pop_back ( );
    }
  }

  return nullptr;
}
