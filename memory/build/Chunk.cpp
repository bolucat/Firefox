/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <cerrno>
#include <cinttypes>
#include <cstdio>

#ifdef XP_WIN
#  include <io.h>
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#endif
#ifdef XP_DARWIN
#  include <libkern/OSAtomic.h>
#  include <mach/mach_init.h>
#  include <mach/vm_map.h>
#endif

#if defined(XP_WIN)
#  include "mozmemory_stall.h"
#endif
#include "Mutex.h"
#include "Chunk.h"
#include "Extent.h"
#include "Globals.h"
#include "RedBlackTree.h"

#include "mozilla/Assertions.h"
#include "mozilla/HelperMacros.h"
// Note: MozTaggedAnonymousMmap() could call an LD_PRELOADed mmap
// instead of the one defined here; use only MozTagAnonymousMemory().
#include "mozilla/TaggedAnonymousMemory.h"
#include "mozilla/ThreadSafety.h"

// For GetGeckoProcessType(), when it's used.
#if defined(XP_WIN) && !defined(JS_STANDALONE)
#  include "mozilla/ProcessType.h"
#endif

using namespace mozilla;

// On Windows, delay crashing on OOM.
#ifdef XP_WIN

// Implementation of VirtualAlloc wrapper (bug 1716727).
namespace MozAllocRetries {

// Maximum retry count on OOM.
constexpr size_t kMaxAttempts = 10;
// Minimum delay time between retries. (The actual delay time may be larger. See
// Microsoft's documentation for ::Sleep() for details.)
constexpr size_t kDelayMs = 50;

using StallSpecs = ::mozilla::StallSpecs;

static constexpr StallSpecs maxStall = {.maxAttempts = kMaxAttempts,
                                        .delayMs = kDelayMs};

static inline StallSpecs GetStallSpecs() {
#  if defined(JS_STANDALONE)
  // GetGeckoProcessType() isn't available in this configuration. (SpiderMonkey
  // on Windows mostly skips this in favor of directly calling ::VirtualAlloc(),
  // though, so it's probably not going to matter whether we stall here or not.)
  return maxStall;
#  else
  switch (GetGeckoProcessType()) {
    // For the main process, stall for the maximum permissible time period. (The
    // main process is the most important one to keep alive.)
    case GeckoProcessType::GeckoProcessType_Default:
      return maxStall;

    // For all other process types, stall for at most half as long.
    default:
      return {.maxAttempts = maxStall.maxAttempts / 2,
              .delayMs = maxStall.delayMs};
  }
#  endif
}

}  // namespace MozAllocRetries

namespace mozilla {

StallSpecs GetAllocatorStallSpecs() {
  return ::MozAllocRetries::GetStallSpecs();
}

// Drop-in wrapper around VirtualAlloc. When out of memory, may attempt to stall
// and retry rather than returning immediately, in hopes that the page file is
// about to be expanded by Windows.
//
// Ref:
// https://docs.microsoft.com/en-us/troubleshoot/windows-client/performance/slow-page-file-growth-memory-allocation-errors
// https://hacks.mozilla.org/2022/11/improving-firefox-stability-with-this-one-weird-trick/
void* MozVirtualAlloc(void* lpAddress, size_t dwSize, uint32_t flAllocationType,
                      uint32_t flProtect) {
  using namespace MozAllocRetries;

  DWORD const lastError = ::GetLastError();

  constexpr auto IsOOMError = [] {
    switch (::GetLastError()) {
      // This is the usual error result from VirtualAlloc for OOM.
      case ERROR_COMMITMENT_LIMIT:
      // Although rare, this has also been observed in low-memory situations.
      // (Presumably this means Windows can't allocate enough kernel-side space
      // for its own internal representation of the process's virtual address
      // space.)
      case ERROR_NOT_ENOUGH_MEMORY:
        return true;
    }
    return false;
  };

  {
    void* ptr = ::VirtualAlloc(lpAddress, dwSize, flAllocationType, flProtect);
    if (MOZ_LIKELY(ptr)) return ptr;

    // We can't do anything for errors other than OOM...
    if (!IsOOMError()) return nullptr;
    // ... or if this wasn't a request to commit memory in the first place.
    // (This function has no strategy for resolving MEM_RESERVE failures.)
    if (!(flAllocationType & MEM_COMMIT)) return nullptr;
  }

  // Retry as many times as desired (possibly zero).
  const StallSpecs stallSpecs = GetStallSpecs();

  const auto ret =
      stallSpecs.StallAndRetry(&::Sleep, [&]() -> std::optional<void*> {
        void* ptr =
            ::VirtualAlloc(lpAddress, dwSize, flAllocationType, flProtect);

        if (ptr) {
          // The OOM status has been handled, and should not be reported to
          // telemetry.
          if (IsOOMError()) {
            ::SetLastError(lastError);
          }
          return ptr;
        }

        // Failure for some reason other than OOM.
        if (!IsOOMError()) {
          return nullptr;
        }

        return std::nullopt;
      });

  return ret.value_or(nullptr);
}

}  // namespace mozilla

#endif  // XP_WIN

// Begin chunk management functions.

// Some tools, such as /dev/dsp wrappers, LD_PRELOAD libraries that
// happen to override mmap() and call dlsym() from their overridden
// mmap(). The problem is that dlsym() calls malloc(), and this ends
// up in a dead lock in jemalloc.
// On these systems, we prefer to directly use the system call.
// We do that for Linux systems and kfreebsd with GNU userland.
// Note sanity checks are not done (alignment of offset, ...) because
// the uses of mmap are pretty limited, in jemalloc.
//
// On Alpha, glibc has a bug that prevents syscall() to work for system
// calls with 6 arguments.
#if (defined(XP_LINUX) && !defined(__alpha__)) || \
    (defined(__FreeBSD_kernel__) && defined(__GLIBC__))
#  include <sys/syscall.h>
#  if defined(SYS_mmap) || defined(SYS_mmap2)
static inline void* _mmap(void* addr, size_t length, int prot, int flags,
                          int fd, off_t offset) {
// S390 only passes one argument to the mmap system call, which is a
// pointer to a structure containing the arguments.
#    ifdef __s390__
  struct {
    void* addr;
    size_t length;
    long prot;
    long flags;
    long fd;
    off_t offset;
  } args = {addr, length, prot, flags, fd, offset};
  return (void*)syscall(SYS_mmap, &args);
#    else
#      if defined(ANDROID) && defined(__aarch64__) && defined(SYS_mmap2)
  // Android NDK defines SYS_mmap2 for AArch64 despite it not supporting mmap2.
#        undef SYS_mmap2
#      endif
#      ifdef SYS_mmap2
  return (void*)syscall(SYS_mmap2, addr, length, prot, flags, fd, offset >> 12);
#      else
  return (void*)syscall(SYS_mmap, addr, length, prot, flags, fd, offset);
#      endif
#    endif
}
#    define mmap _mmap
#    define munmap(a, l) syscall(SYS_munmap, a, l)
#  endif
#endif

#ifdef XP_WIN

static void* pages_map(void* aAddr, size_t aSize) {
  void* ret = nullptr;
  ret = MozVirtualAlloc(aAddr, aSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  return ret;
}

static void pages_unmap(void* aAddr, size_t aSize) {
  if (VirtualFree(aAddr, 0, MEM_RELEASE) == 0) {
    _malloc_message(_getprogname(), ": (malloc) Error in VirtualFree()\n");
  }
}
#else

static void pages_unmap(void* aAddr, size_t aSize) {
  if (munmap(aAddr, aSize) == -1) {
    char buf[64];

    if (strerror_r(errno, buf, sizeof(buf)) == 0) {
      _malloc_message(_getprogname(), ": (malloc) Error in munmap(): ", buf,
                      "\n");
    }
  }
}

static void* pages_map(void* aAddr, size_t aSize) {
  void* ret;
#  if defined(__ia64__) || \
      (defined(__sparc__) && defined(__arch64__) && defined(__linux__))
  // The JS engine assumes that all allocated pointers have their high 17 bits
  // clear, which ia64's mmap doesn't support directly. However, we can emulate
  // it by passing mmap an "addr" parameter with those bits clear. The mmap will
  // return that address, or the nearest available memory above that address,
  // providing a near-guarantee that those bits are clear. If they are not, we
  // return nullptr below to indicate out-of-memory.
  //
  // The addr is chosen as 0x0000070000000000, which still allows about 120TB of
  // virtual address space.
  //
  // See Bug 589735 for more information.
  bool check_placement = true;
  if (!aAddr) {
    aAddr = (void*)0x0000070000000000;
    check_placement = false;
  }
#  endif

#  if defined(__sparc__) && defined(__arch64__) && defined(__linux__)
  const uintptr_t start = 0x0000070000000000ULL;
  const uintptr_t end = 0x0000800000000000ULL;

  // Copied from js/src/gc/Memory.cpp and adapted for this source
  uintptr_t hint;
  void* region = MAP_FAILED;
  for (hint = start; region == MAP_FAILED && hint + aSize <= end;
       hint += kChunkSize) {
    region = mmap((void*)hint, aSize, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANON, -1, 0);
    if (region != MAP_FAILED) {
      if (((size_t)region + (aSize - 1)) & 0xffff800000000000) {
        if (munmap(region, aSize)) {
          MOZ_ASSERT(errno == ENOMEM);
        }
        region = MAP_FAILED;
      }
    }
  }
  ret = region;
#  else
  // We don't use MAP_FIXED here, because it can cause the *replacement*
  // of existing mappings, and we only want to create new mappings.
  ret =
      mmap(aAddr, aSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  MOZ_ASSERT(ret);
#  endif
  if (ret == MAP_FAILED) {
    ret = nullptr;
  }
#  if defined(__ia64__) || \
      (defined(__sparc__) && defined(__arch64__) && defined(__linux__))
  // If the allocated memory doesn't have its upper 17 bits clear, consider it
  // as out of memory.
  else if ((long long)ret & 0xffff800000000000) {
    munmap(ret, aSize);
    ret = nullptr;
  }
  // If the caller requested a specific memory location, verify that's what mmap
  // returned.
  else if (check_placement && ret != aAddr) {
#  else
  else if (aAddr && ret != aAddr) {
#  endif
    // We succeeded in mapping memory, but not in the right place.
    pages_unmap(ret, aSize);
    ret = nullptr;
  }
  if (ret) {
    MozTagAnonymousMemory(ret, aSize, "jemalloc");
  }

#  if defined(__ia64__) || \
      (defined(__sparc__) && defined(__arch64__) && defined(__linux__))
  MOZ_ASSERT(!ret || (!check_placement && ret) ||
             (check_placement && ret == aAddr));
#  else
  MOZ_ASSERT(!ret || (!aAddr && ret != aAddr) || (aAddr && ret == aAddr));
#  endif
  return ret;
}
#endif

// ***************************************************************************

void pages_decommit(void* aAddr, size_t aSize) {
#ifdef XP_WIN
  // The region starting at addr may have been allocated in multiple calls
  // to VirtualAlloc and recycled, so decommitting the entire region in one
  // go may not be valid. However, since we allocate at least a chunk at a
  // time, we may touch any region in chunksized increments.
  size_t pages_size = std::min(aSize, kChunkSize - GetChunkOffsetForPtr(aAddr));
  while (aSize > 0) {
    // This will cause Access Violation on read and write and thus act as a
    // guard page or region as well.
    if (!VirtualFree(aAddr, pages_size, MEM_DECOMMIT)) {
      MOZ_CRASH();
    }
    aAddr = (void*)((uintptr_t)aAddr + pages_size);
    aSize -= pages_size;
    pages_size = std::min(aSize, kChunkSize);
  }
#else
  if (mmap(aAddr, aSize, PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1,
           0) == MAP_FAILED) {
    // We'd like to report the OOM for our tooling, but we can't allocate
    // memory at this point, so avoid the use of printf.
    const char out_of_mappings[] =
        "[unhandlable oom] Failed to mmap, likely no more mappings "
        "available " __FILE__ " : " MOZ_STRINGIFY(__LINE__);
    if (errno == ENOMEM) {
#  ifndef ANDROID
      fputs(out_of_mappings, stderr);
      fflush(stderr);
#  endif
      MOZ_CRASH_ANNOTATE(out_of_mappings);
    }
    MOZ_REALLY_CRASH(__LINE__);
  }
  MozTagAnonymousMemory(aAddr, aSize, "jemalloc-decommitted");
#endif
}

// Commit pages. Returns whether pages were committed.
[[nodiscard]] bool pages_commit(void* aAddr, size_t aSize) {
#ifdef XP_WIN
  // The region starting at addr may have been allocated in multiple calls
  // to VirtualAlloc and recycled, so committing the entire region in one
  // go may not be valid. However, since we allocate at least a chunk at a
  // time, we may touch any region in chunksized increments.
  size_t pages_size = std::min(aSize, kChunkSize - GetChunkOffsetForPtr(aAddr));
  while (aSize > 0) {
    if (!MozVirtualAlloc(aAddr, pages_size, MEM_COMMIT, PAGE_READWRITE)) {
      return false;
    }
    aAddr = (void*)((uintptr_t)aAddr + pages_size);
    aSize -= pages_size;
    pages_size = std::min(aSize, kChunkSize);
  }
#else
  if (mmap(aAddr, aSize, PROT_READ | PROT_WRITE,
           MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0) == MAP_FAILED) {
    return false;
  }
  MozTagAnonymousMemory(aAddr, aSize, "jemalloc");
#endif
  return true;
}

// Purge and release the pages in the chunk of length `length` at `addr` to
// the OS.
// Returns whether the pages are guaranteed to be full of zeroes when the
// function returns.
// The force_zero argument explicitly requests that the memory is guaranteed
// to be full of zeroes when the function returns.
static bool pages_purge(void* addr, size_t length, bool force_zero) {
  pages_decommit(addr, length);
  return true;
}

// pages_trim, chunk_alloc_mmap_slow and chunk_alloc_mmap were cherry-picked
// from upstream jemalloc 3.4.1 to fix Mozilla bug 956501.

static void* pages_trim(void* addr, size_t alloc_size, size_t leadsize,
                        size_t size) {
  void* ret = (void*)((uintptr_t)addr + leadsize);

  MOZ_ASSERT(alloc_size >= leadsize + size);
#ifdef XP_WIN
  {
    void* new_addr;

    pages_unmap(addr, alloc_size);
    new_addr = pages_map(ret, size);
    if (new_addr == ret) {
      return ret;
    }
    if (new_addr) {
      pages_unmap(new_addr, size);
    }
    return nullptr;
  }
#else
  {
    size_t trailsize = alloc_size - leadsize - size;

    if (leadsize != 0) {
      pages_unmap(addr, leadsize);
    }
    if (trailsize != 0) {
      pages_unmap((void*)((uintptr_t)ret + size), trailsize);
    }
    return ret;
  }
#endif
}

static void* chunk_alloc_mmap_slow(size_t size, size_t alignment) {
  void *ret, *pages;
  size_t alloc_size, leadsize;

  alloc_size = size + alignment - gRealPageSize;
  // Beware size_t wrap-around.
  if (alloc_size < size) {
    return nullptr;
  }
  do {
    pages = pages_map(nullptr, alloc_size);
    if (!pages) {
      return nullptr;
    }
    leadsize =
        ALIGNMENT_CEILING((uintptr_t)pages, alignment) - (uintptr_t)pages;
    ret = pages_trim(pages, alloc_size, leadsize, size);
  } while (!ret);

  MOZ_ASSERT(ret);
  return ret;
}

static void* chunk_alloc_mmap(size_t size, size_t alignment) {
  void* ret;
  size_t offset;

  // Ideally, there would be a way to specify alignment to mmap() (like
  // NetBSD has), but in the absence of such a feature, we have to work
  // hard to efficiently create aligned mappings. The reliable, but
  // slow method is to create a mapping that is over-sized, then trim the
  // excess. However, that always results in one or two calls to
  // pages_unmap().
  //
  // Optimistically try mapping precisely the right amount before falling
  // back to the slow method, with the expectation that the optimistic
  // approach works most of the time.
  ret = pages_map(nullptr, size);
  if (!ret) {
    return nullptr;
  }
  offset = ALIGNMENT_ADDR2OFFSET(ret, alignment);
  if (offset != 0) {
    pages_unmap(ret, size);
    return chunk_alloc_mmap_slow(size, alignment);
  }

  MOZ_ASSERT(ret);
  return ret;
}

AddressRadixTree<(sizeof(void*) << 3) - LOG2(kChunkSize)> gChunkRTree;

// Protects chunk-related data structures.
static Mutex chunks_mtx;

// Trees of chunks that were previously allocated (trees differ only in node
// ordering).  These are used when allocating chunks, in an attempt to re-use
// address space.  Depending on function, different tree orderings are needed,
// which is why there are two trees with the same contents.
static RedBlackTree<extent_node_t, ExtentTreeSzTrait> gChunksBySize
    MOZ_GUARDED_BY(chunks_mtx);
static RedBlackTree<extent_node_t, ExtentTreeTrait> gChunksByAddress
    MOZ_GUARDED_BY(chunks_mtx);

// The current amount of recycled bytes, updated atomically.
Atomic<size_t> gRecycledSize;

void chunks_init() {
  // Initialize chunks data.
  chunks_mtx.Init();
  MOZ_PUSH_IGNORE_THREAD_SAFETY
  gChunksBySize.Init();
  gChunksByAddress.Init();
  MOZ_POP_THREAD_SAFETY
}

#ifdef XP_WIN
// On Windows, calls to VirtualAlloc and VirtualFree must be matched, making it
// awkward to recycle allocations of varying sizes. Therefore we only allow
// recycling when the size equals the chunksize, unless deallocation is entirely
// disabled.
#  define CAN_RECYCLE(size) ((size) == kChunkSize)
#else
#  define CAN_RECYCLE(size) true
#endif

#ifdef MOZ_DEBUG
void chunk_assert_zero(void* aPtr, size_t aSize) {
// Only run this expensive check in a vigilant mode.
#  ifdef MALLOC_DEBUG_VIGILANT
  size_t i;
  size_t* p = (size_t*)(uintptr_t)aPtr;

  for (i = 0; i < aSize / sizeof(size_t); i++) {
    MOZ_ASSERT(p[i] == 0);
  }
#  endif
}
#endif

static void chunk_record(void* aChunk, size_t aSize, ChunkType aType) {
  extent_node_t key;

  if (aType != ZEROED_CHUNK) {
    if (pages_purge(aChunk, aSize, aType == HUGE_CHUNK)) {
      aType = ZEROED_CHUNK;
    }
  }

  // Allocate a node before acquiring chunks_mtx even though it might not
  // be needed, because TypedBaseAlloc::alloc() may cause a new base chunk to
  // be allocated, which could cause deadlock if chunks_mtx were already
  // held.
  UniqueBaseNode xnode(ExtentAlloc::alloc());
  // Use xprev to implement conditional deferred deallocation of prev.
  UniqueBaseNode xprev;

  // RAII deallocates xnode and xprev defined above after unlocking
  // in order to avoid potential dead-locks
  MutexAutoLock lock(chunks_mtx);
  key.mAddr = (void*)((uintptr_t)aChunk + aSize);
  extent_node_t* node = gChunksByAddress.SearchOrNext(&key);
  // Try to coalesce forward.
  if (node && node->mAddr == key.mAddr) {
    // Coalesce chunk with the following address range.  This does
    // not change the position within gChunksByAddress, so only
    // remove/insert from/into gChunksBySize.
    gChunksBySize.Remove(node);
    node->mAddr = aChunk;
    node->mSize += aSize;
    if (node->mChunkType != aType) {
      node->mChunkType = RECYCLED_CHUNK;
    }
    gChunksBySize.Insert(node);
  } else {
    // Coalescing forward failed, so insert a new node.
    if (!xnode) {
      // TypedBaseAlloc::alloc() failed, which is an exceedingly
      // unlikely failure.  Leak chunk; its pages have
      // already been purged, so this is only a virtual
      // memory leak.
      return;
    }
    node = xnode.release();
    node->mAddr = aChunk;
    node->mSize = aSize;
    node->mChunkType = aType;
    gChunksByAddress.Insert(node);
    gChunksBySize.Insert(node);
  }

  // Try to coalesce backward.
  extent_node_t* prev = gChunksByAddress.Prev(node);
  if (prev && (void*)((uintptr_t)prev->mAddr + prev->mSize) == aChunk) {
    // Coalesce chunk with the previous address range.  This does
    // not change the position within gChunksByAddress, so only
    // remove/insert node from/into gChunksBySize.
    gChunksBySize.Remove(prev);
    gChunksByAddress.Remove(prev);

    gChunksBySize.Remove(node);
    node->mAddr = prev->mAddr;
    node->mSize += prev->mSize;
    if (node->mChunkType != prev->mChunkType) {
      node->mChunkType = RECYCLED_CHUNK;
    }
    gChunksBySize.Insert(node);

    xprev.reset(prev);
  }

  gRecycledSize += aSize;
}

void chunk_dealloc(void* aChunk, size_t aSize, ChunkType aType) {
  MOZ_ASSERT(aChunk);
  MOZ_ASSERT(GetChunkOffsetForPtr(aChunk) == 0);
  MOZ_ASSERT(aSize != 0);
  MOZ_ASSERT((aSize & kChunkSizeMask) == 0);

  gChunkRTree.Unset(aChunk);

  if (CAN_RECYCLE(aSize)) {
    size_t recycled_so_far = gRecycledSize;
    // In case some race condition put us above the limit.
    if (recycled_so_far < gRecycleLimit) {
      size_t recycle_remaining = gRecycleLimit - recycled_so_far;
      size_t to_recycle;
      if (aSize > recycle_remaining) {
        to_recycle = recycle_remaining;
        // Drop pages that would overflow the recycle limit
        pages_trim(aChunk, aSize, 0, to_recycle);
      } else {
        to_recycle = aSize;
      }
      chunk_record(aChunk, to_recycle, aType);
      return;
    }
  }

  pages_unmap(aChunk, aSize);
}

static void* chunk_recycle(size_t aSize, size_t aAlignment) {
  extent_node_t key;

  size_t alloc_size = aSize + aAlignment - kChunkSize;
  // Beware size_t wrap-around.
  if (alloc_size < aSize) {
    return nullptr;
  }
  key.mAddr = nullptr;
  key.mSize = alloc_size;
  chunks_mtx.Lock();
  extent_node_t* node = gChunksBySize.SearchOrNext(&key);
  if (!node) {
    chunks_mtx.Unlock();
    return nullptr;
  }
  size_t leadsize = ALIGNMENT_CEILING((uintptr_t)node->mAddr, aAlignment) -
                    (uintptr_t)node->mAddr;
  MOZ_ASSERT(node->mSize >= leadsize + aSize);
  size_t trailsize = node->mSize - leadsize - aSize;
  void* ret = (void*)((uintptr_t)node->mAddr + leadsize);

  // All recycled chunks are zeroed (because they're purged) before being
  // recycled.
  MOZ_ASSERT(node->mChunkType == ZEROED_CHUNK);

  // Remove node from the tree.
  gChunksBySize.Remove(node);
  gChunksByAddress.Remove(node);
  if (leadsize != 0) {
    // Insert the leading space as a smaller chunk.
    node->mSize = leadsize;
    gChunksBySize.Insert(node);
    gChunksByAddress.Insert(node);
    node = nullptr;
  }
  if (trailsize != 0) {
    // Insert the trailing space as a smaller chunk.
    if (!node) {
      // An additional node is required, but
      // TypedBaseAlloc::alloc() can cause a new base chunk to be
      // allocated.  Drop chunks_mtx in order to avoid
      // deadlock, and if node allocation fails, deallocate
      // the result before returning an error.
      chunks_mtx.Unlock();
      node = ExtentAlloc::alloc();
      if (!node) {
        chunk_dealloc(ret, aSize, ZEROED_CHUNK);
        return nullptr;
      }
      chunks_mtx.Lock();
    }
    node->mAddr = (void*)((uintptr_t)(ret) + aSize);
    node->mSize = trailsize;
    node->mChunkType = ZEROED_CHUNK;
    gChunksBySize.Insert(node);
    gChunksByAddress.Insert(node);
    node = nullptr;
  }

  gRecycledSize -= aSize;

  chunks_mtx.Unlock();

  if (node) {
    ExtentAlloc::dealloc(node);
  }
  if (!pages_commit(ret, aSize)) {
    return nullptr;
  }

  return ret;
}

// Allocates `size` bytes of system memory aligned for `alignment`.
// `base` indicates whether the memory will be used for the base allocator
// (e.g. base_alloc).
// `zeroed` is an outvalue that returns whether the allocated memory is
// guaranteed to be full of zeroes. It can be omitted when the caller doesn't
// care about the result.
void* chunk_alloc(size_t aSize, size_t aAlignment, bool aBase) {
  void* ret = nullptr;

  MOZ_ASSERT(aSize != 0);
  MOZ_ASSERT((aSize & kChunkSizeMask) == 0);
  MOZ_ASSERT(aAlignment != 0);
  MOZ_ASSERT((aAlignment & kChunkSizeMask) == 0);

  // Base allocations can't be fulfilled by recycling because of
  // possible deadlock or infinite recursion.
  if (CAN_RECYCLE(aSize) && !aBase) {
    ret = chunk_recycle(aSize, aAlignment);
  }
  if (!ret) {
    ret = chunk_alloc_mmap(aSize, aAlignment);
  }
  if (ret && !aBase) {
    if (!gChunkRTree.Set(ret, ret)) {
      chunk_dealloc(ret, aSize, UNKNOWN_CHUNK);
      return nullptr;
    }
  }

  MOZ_ASSERT(GetChunkOffsetForPtr(ret) == 0);
  return ret;
}

// This would be all alone in an Extent.cpp file, instead put it here where
// it is used.
template <>
extent_node_t* ExtentAlloc::sFirstFree = nullptr;

arena_chunk_t::arena_chunk_t(arena_t* aArena)
    : mArena(aArena), mDirtyRunHint(gChunkHeaderNumPages) {}

bool arena_chunk_t::IsEmpty() {
  return (mPageMap[gChunkHeaderNumPages].bits &
          (~gPageSizeMask | CHUNK_MAP_ALLOCATED)) == gMaxLargeClass;
}
