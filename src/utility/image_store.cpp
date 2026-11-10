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

extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;

namespace
{
  // A request, handed to its worker thread
  struct job_s {
    int                request    = 0;
    unsigned int       generation = 0;
    SKIV_DecodeRequest decode;
  };

  void
  _PushForRelease (CComPtr <ID3D11ShaderResourceView>& srv)
  {
    if (srv.p != nullptr)
    {
      PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << srv.p << " to be released";
      SKIF_ResourcesToFree.push (srv.Detach ( ));
    }
  }

  void
  _PushForRelease (CComPtr <ID3D11UnorderedAccessView>& uav)
  {
    if (uav.p != nullptr)
    {
      PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << uav.p << " to be released";
      SKIF_ResourcesToFree.push (uav.Detach ( ));
    }
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

void
SKIV_ImageStore::Request (const std::wstring& path, bool animatedImages)
{
  const int request = m_latest.fetch_add (1) + 1;
  m_pending         = request;
  m_pendingPath     = path;
  m_pendingAnimated = animatedImages;

  job_s* job = new job_s;

  job->request             = request;
  job->generation          = m_generation.load ( );
  job->decode.path           = path;
  job->decode.animatedImages = animatedImages;

  // Animated images are stopped between frames once another image is to be shown instead
  std::atomic <int>* latest = &m_latest;
  job->decode.wanted = [latest, request](void) -> bool { return latest->load ( ) == request; };

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

    result->request    = job->request;
    result->generation = job->generation;

    m_results.push (result);
    delete job;
  }
}

unsigned __stdcall
SKIV_ImageStore::Worker (void* var)
{
  SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_TIME_CRITICAL);

  SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIV_ImageWorker");

  job_s*           job   = static_cast <job_s*> (var);
  SKIV_ImageStore& store = SKIV_ImageStore::Get ( );

  CoInitializeEx (nullptr, 0x0);

  PLOG_DEBUG << "SKIV_ImageWorker thread started!";

  PLOG_INFO  << "Streaming game cover asynchronously...";

  result_s* result = new result_s;

  result->request    = job->request;
  result->generation = job->generation;

  std::vector <ImGuiToast> notices;
  result->success = SKIV_DecodeImage (job->decode, result->image, notices);

  for (const auto& notice : notices)
    store.m_notices.push (notice);

  PLOG_VERBOSE << "_pRawTexSRV = " << result->image.pRawTexSRV;

  const int latest = store.m_latest.load ( );

  if (latest == job->request)
  {
    if (result->success)
      PLOG_VERBOSE << "Queue position is live, and texture was successfully loaded!";
    else
      PLOG_WARNING << "Queue position is live, but texture failed to load properly...";

    store.m_results.push (result);

    // Wakes the main loop (and refreshes the window) to take it
    PostMessage (SKIF_Notify_hWnd, WM_SKIF_IMAGE, SKIV_IMAGE_STORE_RESULT, static_cast <LPARAM> (result->success));
  }

  else
  {
    if (result->image.pRawTexSRV.p != nullptr)
      PLOG_DEBUG << "Texture is late! (" << job->request << " vs " << latest << ")";

    Release (result->image);
    delete result;
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

  if (m_pending == 0)
    return false;

  const std::wstring path = m_pendingPath; // (Request ( ) sets it again)
  Request (path, m_pendingAnimated);

  return true;
}

bool
SKIV_ImageStore::TakeResult (image_s& image, bool& success)
{
  bool      taken  = false;
  result_s* result = nullptr;

  while (m_results.try_pop (result))
  {
    // Only the latest request is shown, and only if it was made on the current device
    if (result->request    == m_latest.load ( )     &&
        result->request    == m_pending             &&
        result->generation == m_generation.load ( ))
    {
      Release (image);

      image.file_info         = result->image.file_info;
      image.bpc               = result->image.bpc;
      image.channels          = result->image.channels;
      image.width             = result->image.width;
      image.height            = result->image.height;
      image.uv0               = result->image.uv0;
      image.uv1               = result->image.uv1;
      image.pRawTexSRV        = result->image.pRawTexSRV;
      image.pGamutCoverageSRV = result->image.pGamutCoverageSRV;
      image.pGamutCoverageUAV = result->image.pGamutCoverageUAV;
      image.light_info        = result->image.light_info;
      image.colorimetry       = result->image.colorimetry;
      image.is_hdr            = result->image.is_hdr;
      image.is_wcg            = result->image.is_wcg;
      image.is_dds            = result->image.is_dds;
      image.anim              = std::move (result->image.anim);
      image.color_source      = std::move (result->image.color_source);

      success   = result->success;
      m_pending = 0;
      taken     = true;
    }

    else
    {
      if (result->image.pRawTexSRV.p != nullptr)
        PLOG_DEBUG << "Texture is late! (" << result->request << " vs " << m_latest.load ( ) << ")";

      Release (result->image);
    }

    delete result; // Its references (image holds its own)
  }

  return taken;
}
