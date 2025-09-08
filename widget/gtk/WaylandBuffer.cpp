/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WaylandBuffer.h"
#include "WaylandSurface.h"
#include "WaylandSurfaceLock.h"

#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>

#include "gfx2DGlue.h"
#include "gfxPlatform.h"
#include "mozilla/WidgetUtilsGtk.h"
#include "mozilla/gfx/Tools.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/ipc/SharedMemoryHandle.h"
#include "nsGtkUtils.h"
#include "nsPrintfCString.h"
#include "prenv.h"  // For PR_GetEnv

#ifdef MOZ_LOGGING
#  include "mozilla/Logging.h"
#  include "mozilla/ScopeExit.h"
#  include "Units.h"
extern mozilla::LazyLogModule gWidgetWaylandLog;
#  define LOGWAYLAND(...) \
    MOZ_LOG(gWidgetWaylandLog, mozilla::LogLevel::Debug, (__VA_ARGS__))
#else
#  define LOGWAYLAND(...)
#endif /* MOZ_LOGGING */

using namespace mozilla::gl;

namespace mozilla::widget {

#define BUFFER_BPP 4

#ifdef MOZ_LOGGING
MOZ_RUNINIT int WaylandBuffer::mDumpSerial =
    PR_GetEnv("MOZ_WAYLAND_DUMP_WL_BUFFERS") ? 1 : 0;
MOZ_RUNINIT char* WaylandBuffer::mDumpDir = PR_GetEnv("MOZ_WAYLAND_DUMP_DIR");
#endif

/* static */
RefPtr<WaylandShmPool> WaylandShmPool::Create(nsWaylandDisplay* aWaylandDisplay,
                                              int aSize) {
  if (!aWaylandDisplay->GetShm()) {
    NS_WARNING("WaylandShmPool: Missing Wayland shm interface!");
    return nullptr;
  }

  RefPtr<WaylandShmPool> shmPool = new WaylandShmPool();

  auto handle = ipc::shared_memory::Create(aSize);
  if (!handle) {
    NS_WARNING("WaylandShmPool: Unable to allocate shared memory!");
    return nullptr;
  }

  shmPool->mShmHandle = handle.Clone();
  shmPool->mShmPool =
      wl_shm_create_pool(aWaylandDisplay->GetShm(),
                         handle.Clone().TakePlatformHandle().get(), aSize);
  if (!shmPool->mShmPool) {
    NS_WARNING("WaylandShmPool: Unable to allocate shared memory pool!");
    return nullptr;
  }

  return shmPool;
}

void* WaylandShmPool::GetImageData() {
  if (!mShm) {
    mShm = mShmHandle.Map();
    if (!mShm) {
      NS_WARNING("WaylandShmPool: Failed to map Shm!");
      return nullptr;
    }
  }
  return mShm.Address();
}

WaylandShmPool::~WaylandShmPool() {
  MozClearPointer(mShmPool, wl_shm_pool_destroy);
}

WaylandBuffer::WaylandBuffer(const LayoutDeviceIntSize& aSize) : mSize(aSize) {}

bool WaylandBuffer::IsAttached() const {
  for (const auto& transaction : mBufferTransactions) {
    if (transaction->IsAttached()) {
      return true;
    }
  }
  return false;
}

BufferTransaction* WaylandBuffer::GetTransaction(
    const WaylandSurfaceLock& aSurfaceLock) {
  for (const auto& transaction : mBufferTransactions) {
    if (transaction->CanRecycle(aSurfaceLock.GetWaylandSurface())) {
      LOGWAYLAND("WaylandBuffer::GetTransaction() [%p] reuse transaction [%d]",
                 (void*)this, (int)mBufferTransactions.Length());
      return transaction;
    }
  }

  wl_buffer* buffer = mExternalWlBuffer;
  if (!buffer) {
    buffer = CreateWlBuffer();
  }
  if (!buffer) {
    gfxCriticalError()
        << "WaylandBuffer::GetTransaction() failed to create wl_buffer!";
    return nullptr;
  }

  LOGWAYLAND(
      "WaylandBuffer::GetTransaction() create new [%p] wl_buffer [%p] "
      "transactions [%d] external buffer [%d] WaylandSurface [%p]",
      (void*)this, buffer, (int)mBufferTransactions.Length(),
      !!mExternalWlBuffer, aSurfaceLock.GetWaylandSurface());

  auto* transaction = new BufferTransaction(this, buffer, !!mExternalWlBuffer);
  mBufferTransactions.AppendElement(transaction);
  return transaction;
}

void WaylandBuffer::RemoveTransaction(RefPtr<BufferTransaction> aTransaction) {
  LOGWAYLAND("WaylandBuffer::RemoveTransaction() [%p]", (void*)aTransaction);
  [[maybe_unused]] bool removed =
      mBufferTransactions.RemoveElement(aTransaction);
  MOZ_DIAGNOSTIC_ASSERT(removed);
  MOZ_DIAGNOSTIC_ASSERT(!mBufferTransactions.Contains(aTransaction));
}

void WaylandBuffer::SetExternalWLBuffer(wl_buffer* aWLBuffer) {
  LOGWAYLAND("WaylandBuffer::SetExternalWLBuffer() [%p] wl_buffer %p",
             (void*)this, aWLBuffer);
  MOZ_DIAGNOSTIC_ASSERT(!mExternalWlBuffer);
  mExternalWlBuffer = aWLBuffer;
}

/* static */
RefPtr<WaylandBufferSHM> WaylandBufferSHM::Create(
    const LayoutDeviceIntSize& aSize) {
  RefPtr<WaylandBufferSHM> buffer = new WaylandBufferSHM(aSize);
  nsWaylandDisplay* waylandDisplay = WaylandDisplayGet();

  LOGWAYLAND("WaylandBufferSHM::Create() [%p] [%d x %d]", (void*)buffer,
             aSize.width, aSize.height);

  int size = aSize.width * aSize.height * BUFFER_BPP;
  buffer->mShmPool = WaylandShmPool::Create(waylandDisplay, size);
  if (!buffer->mShmPool) {
    LOGWAYLAND("  failed to create shmPool");
    return nullptr;
  }

  LOGWAYLAND("  created [%p] WaylandDisplay [%p]\n", buffer.get(),
             waylandDisplay);

  return buffer;
}

wl_buffer* WaylandBufferSHM::CreateWlBuffer() {
  MOZ_DIAGNOSTIC_ASSERT(!mExternalWlBuffer);

  auto* buffer = wl_shm_pool_create_buffer(
      mShmPool->GetShmPool(), 0, mSize.width, mSize.height,
      mSize.width * BUFFER_BPP, WL_SHM_FORMAT_ARGB8888);

  LOGWAYLAND("WaylandBufferSHM::CreateWlBuffer() [%p] wl_buffer [%p]",
             (void*)this, buffer);

  return buffer;
}

WaylandBufferSHM::WaylandBufferSHM(const LayoutDeviceIntSize& aSize)
    : WaylandBuffer(aSize) {
  LOGWAYLAND("WaylandBufferSHM::WaylandBufferSHM() [%p]\n", (void*)this);
}

WaylandBufferSHM::~WaylandBufferSHM() {
  LOGWAYLAND("WaylandBufferSHM::~WaylandBufferSHM() [%p]\n", (void*)this);
  MOZ_RELEASE_ASSERT(!IsAttached());
}

already_AddRefed<gfx::DrawTarget> WaylandBufferSHM::Lock() {
  LOGWAYLAND("WaylandBufferSHM::lock() [%p]\n", (void*)this);
  return gfxPlatform::CreateDrawTargetForData(
      static_cast<unsigned char*>(mShmPool->GetImageData()),
      mSize.ToUnknownSize(), BUFFER_BPP * mSize.width, GetSurfaceFormat());
}

void WaylandBufferSHM::Clear() {
  LOGWAYLAND("WaylandBufferSHM::Clear() [%p]\n", (void*)this);
  memset(mShmPool->GetImageData(), 0xff,
         mSize.height * mSize.width * BUFFER_BPP);
}

#ifdef MOZ_LOGGING
void WaylandBufferSHM::DumpToFile(const char* aHint) {
  if (!mDumpSerial) {
    return;
  }

  cairo_surface_t* surface = nullptr;
  auto unmap = MakeScopeExit([&] {
    if (surface) {
      cairo_surface_destroy(surface);
    }
  });
  surface = cairo_image_surface_create_for_data(
      (unsigned char*)mShmPool->GetImageData(), CAIRO_FORMAT_ARGB32,
      mSize.width, mSize.height, BUFFER_BPP * mSize.width);
  if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
    nsCString filename;
    if (mDumpDir) {
      filename.Append(mDumpDir);
      filename.Append('/');
    }
    filename.Append(nsPrintfCString("firefox-wl-sw-buffer-%.5d-%s.png",
                                    mDumpSerial++, aHint));
    cairo_surface_write_to_png(surface, filename.get());
    LOGWAYLAND("Dumped wl_buffer to %s\n", filename.get());
  }
}
#endif

/* static */
already_AddRefed<WaylandBufferDMABUF> WaylandBufferDMABUF::CreateRGBA(
    const LayoutDeviceIntSize& aSize, GLContext* aGL,
    RefPtr<DRMFormat> aFormat) {
  RefPtr<WaylandBufferDMABUF> buffer = new WaylandBufferDMABUF(aSize);

  buffer->mDMABufSurface = DMABufSurfaceRGBA::CreateDMABufSurface(
      aGL, aSize.width, aSize.height, DMABUF_SCANOUT | DMABUF_USE_MODIFIERS,
      aFormat);
  if (!buffer->mDMABufSurface || !buffer->mDMABufSurface->CreateTexture(aGL)) {
    LOGWAYLAND("  failed to create texture");
    return nullptr;
  }

  LOGWAYLAND("WaylandBufferDMABUF::CreateRGBA() [%p] UID %d [%d x %d]",
             (void*)buffer, buffer->mDMABufSurface->GetUID(), aSize.width,
             aSize.height);
  return buffer.forget();
}

/* static */
already_AddRefed<WaylandBufferDMABUF> WaylandBufferDMABUF::CreateExternal(
    RefPtr<DMABufSurface> aSurface) {
  const auto size =
      LayoutDeviceIntSize(aSurface->GetWidth(), aSurface->GetWidth());
  RefPtr<WaylandBufferDMABUF> buffer = new WaylandBufferDMABUF(size);

  LOGWAYLAND("WaylandBufferDMABUF::CreateExternal() [%p] UID %d [%d x %d]",
             (void*)buffer, aSurface->GetUID(), size.width, size.height);

  buffer->mDMABufSurface = aSurface;
  return buffer.forget();
}

wl_buffer* WaylandBufferDMABUF::CreateWlBuffer() {
  MOZ_DIAGNOSTIC_ASSERT(mDMABufSurface);
  MOZ_DIAGNOSTIC_ASSERT(!mExternalWlBuffer);

  auto* buffer = mDMABufSurface->CreateWlBuffer();

  LOGWAYLAND("WaylandBufferDMABUF::CreateWlBuffer() [%p] UID %d wl_buffer [%p]",
             (void*)this, mDMABufSurface->GetUID(), buffer);

  return buffer;
}

WaylandBufferDMABUF::WaylandBufferDMABUF(const LayoutDeviceIntSize& aSize)
    : WaylandBuffer(aSize) {
  LOGWAYLAND("WaylandBufferDMABUF::WaylandBufferDMABUF [%p]\n", (void*)this);
}

WaylandBufferDMABUF::~WaylandBufferDMABUF() {
  LOGWAYLAND("WaylandBufferDMABUF::~WaylandBufferDMABUF [%p] UID %d\n",
             (void*)this, mDMABufSurface ? mDMABufSurface->GetUID() : -1);
  MOZ_RELEASE_ASSERT(!IsAttached());
}

#ifdef MOZ_LOGGING
void WaylandBufferDMABUF::DumpToFile(const char* aHint) {
  if (!mDumpSerial) {
    return;
  }
  nsCString filename;
  if (mDumpDir) {
    filename.Append(mDumpDir);
    filename.Append('/');
  }
  filename.AppendPrintf("firefox-wl-buffer-dmabuf-%.5d-%s.png", mDumpSerial++,
                        aHint);
  mDMABufSurface->DumpToFile(filename.get());
  LOGWAYLAND("Dumped wl_buffer to %s\n", filename.get());
}
#endif

WaylandBufferDMABUFHolder::WaylandBufferDMABUFHolder(DMABufSurface* aSurface,
                                                     wl_buffer* aWLBuffer)
    : mWLBuffer(aWLBuffer) {
  mUID = aSurface->GetUID();
  mPID = aSurface->GetPID();
  LOGWAYLAND(
      "WaylandBufferDMABUFHolder::WaylandBufferDMABUFHolder wl_buffer [%p] UID "
      "%d PID %d",
      mWLBuffer, mUID, mPID);
}

WaylandBufferDMABUFHolder::~WaylandBufferDMABUFHolder() {
  LOGWAYLAND(
      "WaylandBufferDMABUFHolder::~WaylandBufferDMABUFHolder wl_buffer [%p] "
      "UID %d PID %d",
      mWLBuffer, mUID, mPID);
  MozClearPointer(mWLBuffer, wl_buffer_destroy);
}

bool WaylandBufferDMABUFHolder::Matches(DMABufSurface* aSurface) const {
  return mUID == aSurface->GetUID() && mPID == aSurface->GetPID();
}

wl_buffer* BufferTransaction::BufferBorrowLocked(
    const WaylandSurfaceLock& aSurfaceLock) {
  LOGWAYLAND(
      "BufferTransaction::BufferBorrow() [%p] widget [%p] WaylandSurface [%p] "
      "WaylandBuffer [%p]",
      this, aSurfaceLock.GetWaylandSurface()->GetLoggingWidget(),
      aSurfaceLock.GetWaylandSurface(), mBuffer.get());

  MOZ_DIAGNOSTIC_ASSERT(
      !mSurface || mSurface == aSurfaceLock.GetWaylandSurface(),
      "Can't transfer transaction between WaylandSurfaces!");
  MOZ_DIAGNOSTIC_ASSERT(mBufferState == BufferState::Detached);

  mSurface = aSurfaceLock.GetWaylandSurface();

  // We don't take reference to this. Some compositors doesn't send
  // buffer release callback and we may leak BufferTransaction then.
  // Rather we destroy wl_buffer at end which makes sure no release callback
  // comes after BufferTransaction release.
  if (wl_proxy_get_listener((wl_proxy*)mWLBuffer)) {
    wl_proxy_set_user_data((wl_proxy*)mWLBuffer, this);
  } else {
    static const struct wl_buffer_listener listener{
        [](void* aData, wl_buffer* aBuffer) {
          auto transaction = static_cast<BufferTransaction*>(aData);
          if (transaction) {
            // BufferDetachCallback can delete transaction
            RefPtr grip{transaction};
            transaction->BufferDetachCallback();
          }
        }};
    if (wl_buffer_add_listener(mWLBuffer, &listener, this) < 0) {
      gfxCriticalError() << "wl_buffer_add_listener() failed";
    }
  }

  mBufferState = BufferState::WaitingForDetach;
  return mWLBuffer;
}

void BufferTransaction::BufferDetachCallback() {
  WaylandSurfaceLock lock(mSurface);
  LOGWAYLAND(
      "BufferTransaction::BufferDetach() [%p] WaylandBuffer [%p] attached to "
      "WaylandSurface %d",
      this, (void*)mBuffer, mSurface->IsBufferAttached(mBuffer));

  if (mBufferState != BufferState::WaitingForDelete) {
    mBufferState = BufferState::Detached;

    // Delete this transaction if WaylandSurface uses different WaylandBuffer
    // already, we don't need to keep it for recycling.
    if (!mSurface->IsBufferAttached(mBuffer)) {
      DeleteTransactionLocked(lock);
    }
  }
}

void BufferTransaction::BufferDeleteCallback() {
  LOGWAYLAND("BufferTransaction::DeleteCallback() [%p] WaylandBuffer [%p] ",
             this, (void*)mBuffer);
  WaylandSurfaceLock lock(mSurface);
  mBufferState = BufferState::Deleted;
  DeleteLocked(lock);
}

void BufferTransaction::WlBufferDeleteLocked(
    const WaylandSurfaceLock& aSurfaceLock) {
  LOGWAYLAND(
      "BufferTransaction::WlBufferDeleteLocked() [%p] WaylandBuffer [%p] ",
      this, (void*)mBuffer);
  MOZ_DIAGNOSTIC_ASSERT(mWLBuffer);
  if (mIsExternalBuffer) {
    wl_proxy_set_user_data((wl_proxy*)mWLBuffer, nullptr);
    mWLBuffer = nullptr;
  } else {
    MozClearPointer(mWLBuffer, wl_buffer_destroy);
  }
}

void BufferTransaction::DeleteTransactionLocked(
    const WaylandSurfaceLock& aSurfaceLock) {
  // It's possible that the transaction is already deleted. It happens
  // if one WaylandBuffer is attached to WaylandSurface,
  // then detached/deleted from WaylandSurface::UnmapLocked() where all buffers
  // are removed and then attached to another WaylandSurface.
  if (mBufferState == BufferState::WaitingForDelete ||
      mBufferState == BufferState::Deleted) {
    return;
  }

  LOGWAYLAND(
      "BufferTransaction::BufferDelete() [%p] WaylandBuffer [%p] wl_buffer "
      "[%p] "
      "external %d state %d",
      this, (void*)mBuffer, mWLBuffer, mIsExternalBuffer, (int)mBufferState);

  WlBufferDeleteLocked(aSurfaceLock);

  // wl_buffer is detached so we can't get any release event so
  // delete the transaction now.
  if (mBufferState == BufferState::Detached) {
    mBufferState = BufferState::Deleted;
    DeleteLocked(aSurfaceLock);
    return;
  }

  mBufferState = BufferState::WaitingForDelete;

  // There are various Wayland queues processed for every thread.
  // It's possible that wl_buffer release event is pending in any
  // queue while we already asked for wl_buffer delete.
  // We need to finish wl_buffer removal when all events from this
  // point are processed so we use sync callback.
  //
  // When wl_display_sync comes back to us (from main thread)
  // we know all events are processed and there isn't any
  // wl_buffer operation pending so we can safely release WaylandSurface
  // and WaylandBuffer objects.
  // We need to wait with WaylandMonitor
  AddRef();
  static const struct wl_callback_listener listener{
      [](void* aData, struct wl_callback* callback, uint32_t time) {
        RefPtr t = dont_AddRef(static_cast<BufferTransaction*>(aData));
        t->BufferDeleteCallback();
      }};
  wl_callback_add_listener(wl_display_sync(WaylandDisplayGetWLDisplay()),
                           &listener, this);
}

void BufferTransaction::DeleteLocked(const WaylandSurfaceLock& aSurfaceLock) {
  LOGWAYLAND("BufferTransaction::DeleteLocked() [%p] WaylandBuffer [%p]", this,
             (void*)mBuffer);
  MOZ_DIAGNOSTIC_ASSERT(mBufferState == BufferState::Deleted);
  MOZ_DIAGNOSTIC_ASSERT(mSurface);

  // Unlink from Surface
  mSurface->RemoveTransactionLocked(aSurfaceLock, this);
  mSurface = nullptr;

  // This can destroy us
  RefPtr grip{this};
  mBuffer->RemoveTransaction(this);
  mBuffer = nullptr;
}

BufferTransaction::BufferTransaction(WaylandBuffer* aBuffer,
                                     wl_buffer* aWLBuffer,
                                     bool aIsExternalBuffer)
    : mBuffer(aBuffer),
      mWLBuffer(aWLBuffer),
      mIsExternalBuffer(aIsExternalBuffer) {
  MOZ_COUNT_CTOR(BufferTransaction);
  LOGWAYLAND(
      "BufferTransaction::BufferTransaction() [%p] WaylandBuffer [%p] "
      "wl_buffer [%p] external [%d]",
      this, aBuffer, mWLBuffer, mIsExternalBuffer);
}

BufferTransaction::~BufferTransaction() {
  MOZ_COUNT_DTOR(BufferTransaction);
  LOGWAYLAND("BufferTransaction::~BufferTransaction() [%p] ", this);
  MOZ_DIAGNOSTIC_ASSERT(mBufferState == BufferState::Deleted);
  MOZ_DIAGNOSTIC_ASSERT(!mWLBuffer);
}

}  // namespace mozilla::widget
