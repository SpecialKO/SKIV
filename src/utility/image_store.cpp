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
SKIV_ImageStore::Keep (const key_s& key, const image_s& image)
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
  entry->bytes = bytes;
  Copy (image, entry->image);

  m_cache.push_back (std::move (entry));

  PLOG_INFO << "[Image Store] Keeping " << SK_WideCharToUTF8 (key.path) << " (" << (bytes / (1024 * 1024)) << " MB; "
            << (_Total ( ) / (1024 * 1024)) << " MB kept)";
  return true;
}

int
SKIV_ImageStore::Start (const std::wstring& path, const key_s& key, bool animatedImages, bool preload)
{
  job_s* job = new job_s;

  job->job                   = ++m_jobs;
  job->generation            = m_generation.load ( );
  job->preload               = preload;
  job->decode.path           = path;
  job->decode.animatedImages = animatedImages;
  job->wanted                = std::make_shared <std::atomic<bool>> (true);

  // Animated images are stopped between frames once they are no longer wanted
  std::shared_ptr <std::atomic<bool>> wanted = job->wanted;
  job->decode.wanted = [wanted](void) -> bool { return wanted->load ( ); };

  inflight_s inflight;
  inflight.job     = job->job;
  inflight.key     = key;
  inflight.preload = preload;
  inflight.wanted  = job->wanted;
  m_inflight.push_back (inflight);

  if (preload)
    PLOG_INFO << "[Image Store] Preloading " << SK_WideCharToUTF8 (path);

  const int id = job->job;

  HANDLE hWorkerThread = reinterpret_cast <HANDLE> (
    _beginthreadex (nullptr, 0x0, &SKIV_ImageStore::Worker, job, 0x0, nullptr)
  );

  if (hWorkerThread != NULL) // We don't care about how it goes so the handle is unneeded
    CloseHandle (hWorkerThread);

  else
  {
    // No worker: the request has failed (TakeResult reports it)
    PLOG_ERROR << "Failed to start an image worker!";

    result_s* result = new result_s;

    result->job        = job->job;
    result->generation = job->generation;
    result->key        = key;
    result->preload    = preload;

    m_results.push (result);
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

  // Decodes in flight for other files are no longer wanted (their results are still kept, if they turn out to be)
  inflight_s* decoding = nullptr;

  for (auto& inflight : m_inflight)
  {
    if (inflight.key == key)
      decoding = &inflight;
    else
      inflight.wanted->store (false);
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
    m_pending         = decoding->job;
  }

  else
    m_pending = Start (path, key, animatedImages, false);
}

unsigned __stdcall
SKIV_ImageStore::Worker (void* var)
{
  SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_TIME_CRITICAL);

  SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIV_ImageWorker");

  job_s*           job   = static_cast <job_s*> (var);
  SKIV_ImageStore& store = SKIV_ImageStore::Get ( );

  // Preloads give way to the decodes of images to show
  if (job->preload)
    SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_BELOW_NORMAL);

  CoInitializeEx (nullptr, 0x0);

  PLOG_DEBUG << "SKIV_ImageWorker thread started!";

  PLOG_INFO  << "Streaming game cover asynchronously...";

  result_s* result = new result_s;

  result->job        = job->job;
  result->generation = job->generation;
  result->key        = Key (job->decode.path);
  result->preload    = job->preload;

  std::vector <ImGuiToast> notices;
  result->success = SKIV_DecodeImage (job->decode, result->image, notices);

  for (const auto& notice : notices)
    store.m_notices.push (notice);

  PLOG_VERBOSE << "_pRawTexSRV = " << result->image.pRawTexSRV;

  // The main thread decides what becomes of it (TakeResult)
  store.m_results.push (result);

  // Wakes the main loop (and refreshes the window) to take it
  PostMessage (SKIF_Notify_hWnd, WM_SKIF_IMAGE, SKIV_IMAGE_STORE_RESULT, 0);

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

  auto _Show = [&](const image_s& from, bool decoded)
  {
    Release (image);
    Copy    (from, image);

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
    m_inflight.erase (std::remove_if (m_inflight.begin ( ), m_inflight.end ( ),
                        [&](const inflight_s& inflight) { return inflight.job == result->job; }), m_inflight.end ( ));

    // Made on the current device
    if (result->generation == m_generation.load ( ))
    {
      const bool show = (m_pending != 0 && ! m_pendingCached && result->job == m_pending);

      // Kept, if it is wanted (animated images are not: their frames take much more memory)
      if (result->success && result->image.anim == nullptr && (show || IsWanted (result->key)))
      {
        if (! Keep (result->key, result->image) && result->preload)
          m_skip.push_back (result->key);
      }

      else if (result->preload)
        m_skip.push_back (result->key);

      if (show)
        _Show (result->image, result->success);
    }

    Release (result->image);
    delete result;
  }

  // From the cache
  if (m_pending != 0 && m_pendingCached)
  {
    if (entry_s* entry = Find (m_pendingKey))
      _Show (entry->image, true);

    // (No longer kept: decoded after all)
    else
    {
      m_pendingCached = false;
      m_pending       = Start (m_pendingPath, m_pendingKey, m_pendingAnimated, false);
    }
  }

  return taken;
}

void
SKIV_ImageStore::SetNeighbours (const std::wstring& shown, const std::wstring& previous, const std::wstring& next, bool animatedImages)
{
  m_neighbourAnimated = animatedImages;

  if (shown != m_shownPath || previous != m_prevPath || next != m_nextPath)
  {
    m_shownPath = shown;    m_shown = Key (shown);
    m_prevPath  = previous; m_prev  = Key (previous);
    m_nextPath  = next;     m_next  = Key (next);

    m_skip.clear ( );

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
  // One at a time, and only while nothing else is decoding
  if (m_pending != 0 || ! m_inflight.empty ( ))
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
    break;
  }
}
