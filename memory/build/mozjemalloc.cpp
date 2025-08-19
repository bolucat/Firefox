/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Portions of this file were originally under the following license:
//
// Copyright (C) 2006-2008 Jason Evans <jasone@FreeBSD.org>.
// All rights reserved.
// Copyright (C) 2007-2017 Mozilla Foundation.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice(s), this list of conditions and the following disclaimer as
//    the first lines of this file unmodified other than the possible
//    addition of one or more copyright notices.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice(s), this list of conditions and the following disclaimer in
//    the documentation and/or other materials provided with the
//    distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
// BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// *****************************************************************************
//
// This allocator implementation is designed to provide scalable performance
// for multi-threaded programs on multi-processor systems.  The following
// features are included for this purpose:
//
//   + Multiple arenas are used if there are multiple CPUs, which reduces lock
//     contention and cache sloshing.
//
//   + Cache line sharing between arenas is avoided for internal data
//     structures.
//
//   + Memory is managed in chunks and runs (chunks can be split into runs),
//     rather than as individual pages.  This provides a constant-time
//     mechanism for associating allocations with particular arenas.
//
// Allocation requests are rounded up to the nearest size class, and no record
// of the original request size is maintained.  Allocations are broken into
// categories according to size class.  Assuming runtime defaults, the size
// classes in each category are as follows (for x86, x86_64 and Apple Silicon):
//
//   |=========================================================|
//   | Category | Subcategory    |     x86 |  x86_64 | Mac ARM |
//   |---------------------------+---------+---------+---------|
//   | Word size                 |  32 bit |  64 bit |  64 bit |
//   | Page size                 |    4 Kb |    4 Kb |   16 Kb |
//   |=========================================================|
//   | Small    | Tiny           |    4/-w |      -w |       - |
//   |          |                |       8 |    8/-w |       8 |
//   |          |----------------+---------|---------|---------|
//   |          | Quantum-spaced |      16 |      16 |      16 |
//   |          |                |      32 |      32 |      32 |
//   |          |                |      48 |      48 |      48 |
//   |          |                |     ... |     ... |     ... |
//   |          |                |     480 |     480 |     480 |
//   |          |                |     496 |     496 |     496 |
//   |          |----------------+---------|---------|---------|
//   |          | Quantum-wide-  |     512 |     512 |     512 |
//   |          | spaced         |     768 |     768 |     768 |
//   |          |                |     ... |     ... |     ... |
//   |          |                |    3584 |    3584 |    3584 |
//   |          |                |    3840 |    3840 |    3840 |
//   |          |----------------+---------|---------|---------|
//   |          | Sub-page       |       - |       - |    4096 |
//   |          |                |       - |       - |    8 kB |
//   |=========================================================|
//   | Large                     |    4 kB |    4 kB |       - |
//   |                           |    8 kB |    8 kB |       - |
//   |                           |   12 kB |   12 kB |       - |
//   |                           |   16 kB |   16 kB |   16 kB |
//   |                           |     ... |     ... |       - |
//   |                           |   32 kB |   32 kB |   32 kB |
//   |                           |     ... |     ... |     ... |
//   |                           | 1008 kB | 1008 kB | 1008 kB |
//   |                           | 1012 kB | 1012 kB |       - |
//   |                           | 1016 kB | 1016 kB |       - |
//   |                           | 1020 kB | 1020 kB |       - |
//   |=========================================================|
//   | Huge                      |    1 MB |    1 MB |    1 MB |
//   |                           |    2 MB |    2 MB |    2 MB |
//   |                           |    3 MB |    3 MB |    3 MB |
//   |                           |     ... |     ... |     ... |
//   |=========================================================|
//
// Legend:
//   n:    Size class exists for this platform.
//   n/-w: This size class doesn't exist on Windows (see kMinTinyClass).
//   -:    This size class doesn't exist for this platform.
//   ...:  Size classes follow a pattern here.
//
// NOTE: Due to Mozilla bug 691003, we cannot reserve less than one word for an
// allocation on Linux or Mac.  So on 32-bit *nix, the smallest bucket size is
// 4 bytes, and on 64-bit, the smallest bucket size is 8 bytes.
//
// A different mechanism is used for each category:
//
//   Small : Each size class is segregated into its own set of runs.  Each run
//           maintains a bitmap of which regions are free/allocated.
//
//   Large : Each allocation is backed by a dedicated run.  Metadata are stored
//           in the associated arena chunk header maps.
//
//   Huge : Each allocation is backed by a dedicated contiguous set of chunks.
//          Metadata are stored in a separate red-black tree.
//
// *****************************************************************************

#include "mozmemory_wrap.h"
#include "mozjemalloc.h"
#include "mozjemalloc_types.h"
#include "mozjemalloc_profiling.h"

#include <cstring>
#include <cerrno>
#include <chrono>
#include <optional>
#include <type_traits>
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

#include "mozilla/Atomics.h"
#include "mozilla/Alignment.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/DoublyLinkedList.h"
#include "mozilla/HelperMacros.h"
#include "mozilla/Likely.h"
#include "mozilla/Literals.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/RandomNum.h"
#include "mozilla/RefPtr.h"
// Note: MozTaggedAnonymousMmap() could call an LD_PRELOADed mmap
// instead of the one defined here; use only MozTagAnonymousMemory().
#include "mozilla/TaggedAnonymousMemory.h"
#include "mozilla/ThreadLocal.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Unused.h"
#include "mozilla/XorShift128PlusRNG.h"
#include "mozilla/fallible.h"
#include "RadixTree.h"
#include "BaseAlloc.h"
#include "Chunk.h"
#include "Constants.h"
#include "Extent.h"
#include "Globals.h"
#include "Mutex.h"
#include "PHC.h"
#include "RedBlackTree.h"
#include "Utils.h"
#include "Zero.h"

#if defined(XP_WIN)
#  include "mozmemory_stall.h"
#endif

using namespace mozilla;

#ifdef MOZJEMALLOC_PROFILING_CALLBACKS
// MallocProfilerCallbacks is refcounted so that one thread cannot destroy it
// while another thread accesses it.  This means that clearing this value or
// otherwise dropping a reference to it must not be done while holding an
// arena's lock.
MOZ_CONSTINIT static RefPtr<MallocProfilerCallbacks> sCallbacks;
#endif

// ***************************************************************************
// MALLOC_DECOMMIT and MALLOC_DOUBLE_PURGE are mutually exclusive.
#if defined(MALLOC_DECOMMIT) && defined(MALLOC_DOUBLE_PURGE)
#  error MALLOC_DECOMMIT and MALLOC_DOUBLE_PURGE are mutually exclusive.
#endif

// Set to true once the allocator has been initialized.
#if defined(_MSC_VER) && !defined(__clang__)
// MSVC may create a static initializer for an Atomic<bool>, which may actually
// run after `malloc_init` has been called once, which triggers multiple
// initializations.
// We work around the problem by not using an Atomic<bool> at all. There is a
// theoretical problem with using `malloc_initialized` non-atomically, but
// practically, this is only true if `malloc_init` is never called before
// threads are created.
static bool malloc_initialized;
#else
// We can rely on Relaxed here because this variable is only ever set when
// holding gInitLock. A thread that still sees it false while another sets it
// true will enter the same lock, synchronize with the former and check the
// flag again under the lock.
static Atomic<bool, MemoryOrdering::Relaxed> malloc_initialized;
#endif

// This lock must be held while bootstrapping us.
StaticMutex gInitLock MOZ_UNANNOTATED = {STATIC_MUTEX_INIT};

// ***************************************************************************
// Statistics data structures.

struct arena_stats_t {
  // Number of bytes currently mapped.
  size_t mapped;

  // Current number of committed pages (non madvised/decommitted)
  size_t committed;

  // Per-size-category statistics.
  size_t allocated_small;

  size_t allocated_large;

  // The number of "memory operations" aka mallocs/frees.
  uint64_t operations;
};

// Describe size classes to which allocations are rounded up to.
// TODO: add large and huge types when the arena allocation code
// changes in a way that allows it to be beneficial.
class SizeClass {
 public:
  enum ClassType {
    Tiny,
    Quantum,
    QuantumWide,
    SubPage,
    Large,
  };

  explicit inline SizeClass(size_t aSize) {
    if (aSize <= kMaxTinyClass) {
      mType = Tiny;
      mSize = std::max(RoundUpPow2(aSize), kMinTinyClass);
    } else if (aSize <= kMaxQuantumClass) {
      mType = Quantum;
      mSize = QUANTUM_CEILING(aSize);
    } else if (aSize <= kMaxQuantumWideClass) {
      mType = QuantumWide;
      mSize = QUANTUM_WIDE_CEILING(aSize);
    } else if (aSize <= gMaxSubPageClass) {
      mType = SubPage;
      mSize = SUBPAGE_CEILING(aSize);
    } else if (aSize <= gMaxLargeClass) {
      mType = Large;
      mSize = PAGE_CEILING(aSize);
    } else {
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Invalid size");
    }
  }

  SizeClass& operator=(const SizeClass& aOther) = default;

  bool operator==(const SizeClass& aOther) { return aOther.mSize == mSize; }

  size_t Size() { return mSize; }

  ClassType Type() { return mType; }

  SizeClass Next() { return SizeClass(mSize + 1); }

 private:
  ClassType mType;
  size_t mSize;
};

// ***************************************************************************
// Arena data structures.

struct arena_bin_t;

struct ArenaChunkMapLink {
  static RedBlackTreeNode<arena_chunk_map_t>& GetTreeNode(
      arena_chunk_map_t* aThis) {
    return aThis->link;
  }
};

struct ArenaAvailTreeTrait : public ArenaChunkMapLink {
  static inline Order Compare(arena_chunk_map_t* aNode,
                              arena_chunk_map_t* aOther) {
    size_t size1 = aNode->bits & ~gPageSizeMask;
    size_t size2 = aOther->bits & ~gPageSizeMask;
    Order ret = CompareInt(size1, size2);
    return (ret != Order::eEqual)
               ? ret
               : CompareAddr((aNode->bits & CHUNK_MAP_KEY) ? nullptr : aNode,
                             aOther);
  }
};

struct ArenaDirtyChunkTrait {
  static RedBlackTreeNode<arena_chunk_t>& GetTreeNode(arena_chunk_t* aThis) {
    return aThis->mLinkDirty;
  }

  static inline Order Compare(arena_chunk_t* aNode, arena_chunk_t* aOther) {
    MOZ_ASSERT(aNode);
    MOZ_ASSERT(aOther);
    return CompareAddr(aNode, aOther);
  }
};

#ifdef MALLOC_DOUBLE_PURGE
namespace mozilla {

template <>
struct GetDoublyLinkedListElement<arena_chunk_t> {
  static DoublyLinkedListElement<arena_chunk_t>& Get(arena_chunk_t* aThis) {
    return aThis->mChunksMavisedElim;
  }
};
}  // namespace mozilla
#endif

enum class purge_action_t {
  None,
  PurgeNow,
  Queue,
};

struct arena_run_t {
#if defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
  uint32_t mMagic;
#  define ARENA_RUN_MAGIC 0x384adf93

  // On 64-bit platforms, having the arena_bin_t pointer following
  // the mMagic field means there's padding between both fields, making
  // the run header larger than necessary.
  // But when MOZ_DIAGNOSTIC_ASSERT_ENABLED is not set, starting the
  // header with this field followed by the arena_bin_t pointer yields
  // the same padding. We do want the mMagic field to appear first, so
  // depending whether MOZ_DIAGNOSTIC_ASSERT_ENABLED is set or not, we
  // move some field to avoid padding.

  // Number of free regions in run.
  unsigned mNumFree;
#endif

  // Used by arena_bin_t::mNonFullRuns.
  DoublyLinkedListElement<arena_run_t> mRunListElem;

  // Bin this run is associated with.
  arena_bin_t* mBin;

  // Index of first element that might have a free region.
  unsigned mRegionsMinElement;

#if !defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
  // Number of free regions in run.
  unsigned mNumFree;
#endif

  // Bitmask of in-use regions (0: in use, 1: free).
  unsigned mRegionsMask[];  // Dynamically sized.
};

namespace mozilla {

template <>
struct GetDoublyLinkedListElement<arena_run_t> {
  static DoublyLinkedListElement<arena_run_t>& Get(arena_run_t* aThis) {
    return aThis->mRunListElem;
  }
};

}  // namespace mozilla

struct arena_bin_t {
  // We use a LIFO ("last-in-first-out") policy to refill non-full runs.
  //
  // This has the following reasons:
  // 1. It is cheap, as all our non-full-runs' book-keeping is O(1), no
  //    tree-balancing or walking is needed.
  // 2. It also helps to increase the probability for CPU cache hits for the
  //    book-keeping and the reused slots themselves, as the same memory was
  //    most recently touched during free, especially when used from the same
  //    core (or via the same shared cache, depending on the architecture).
  DoublyLinkedList<arena_run_t> mNonFullRuns;

  // Bin's size class.
  size_t mSizeClass;

  // Total number of regions in a run for this bin's size class.
  uint32_t mRunNumRegions;

  // Number of elements in a run's mRegionsMask for this bin's size class.
  uint32_t mRunNumRegionsMask;

  // Offset of first region in a run for this bin's size class.
  uint32_t mRunFirstRegionOffset;

  // Current number of runs in this bin, full or otherwise.
  uint32_t mNumRuns;

  // A constant for fast division by size class.  This value is 16 bits wide so
  // it is placed last.
  FastDivisor<uint16_t> mSizeDivisor;

  // Total number of pages in a run for this bin's size class.
  uint8_t mRunSizePages;

  // Amount of overhead runs are allowed to have.
  static constexpr double kRunOverhead = 1.6_percent;
  static constexpr double kRunRelaxedOverhead = 2.4_percent;

  // Initialize a bin for the given size class.
  // The generated run sizes, for a page size of 4 KiB, are:
  //   size|run       size|run       size|run       size|run
  //  class|size     class|size     class|size     class|size
  //     4   4 KiB      8   4 KiB     16   4 KiB     32   4 KiB
  //    48   4 KiB     64   4 KiB     80   4 KiB     96   4 KiB
  //   112   4 KiB    128   8 KiB    144   4 KiB    160   8 KiB
  //   176   4 KiB    192   4 KiB    208   8 KiB    224   4 KiB
  //   240   8 KiB    256  16 KiB    272   8 KiB    288   4 KiB
  //   304  12 KiB    320  12 KiB    336   4 KiB    352   8 KiB
  //   368   4 KiB    384   8 KiB    400  20 KiB    416  16 KiB
  //   432  12 KiB    448   4 KiB    464  16 KiB    480   8 KiB
  //   496  20 KiB    512  32 KiB    768  16 KiB   1024  64 KiB
  //  1280  24 KiB   1536  32 KiB   1792  16 KiB   2048 128 KiB
  //  2304  16 KiB   2560  48 KiB   2816  36 KiB   3072  64 KiB
  //  3328  36 KiB   3584  32 KiB   3840  64 KiB
  inline void Init(SizeClass aSizeClass);
};

// We try to keep the above structure aligned with common cache lines sizes,
// often that's 64 bytes on x86 and ARM, we don't make assumptions for other
// architectures.
#if defined(__x86_64__) || defined(__aarch64__)
// On 64bit platforms this structure is often 48 bytes
// long, which means every other array element will be properly aligned.
static_assert(sizeof(arena_bin_t) == 48);
#elif defined(__x86__) || defined(__arm__)
static_assert(sizeof(arena_bin_t) == 32);
#endif

// We cannot instantiate
// Atomic<std::chrono::time_point<std::chrono::steady_clock>>
// so we explicitly force timestamps to be uint64_t in ns.
uint64_t GetTimestampNS() {
  // On most if not all systems we care about the conversion to ns is a no-op,
  // so we prefer to keep the precision here for performance, but let's be
  // explicit about it.
  return std::chrono::floor<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now())
      .time_since_epoch()
      .count();
}

enum PurgeCondition { PurgeIfThreshold, PurgeUnconditional };

struct arena_t {
#if defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
  uint32_t mMagic;
#  define ARENA_MAGIC 0x947d3d24
#endif

  // Linkage for the tree of arenas by id.
  // This just provides the memory to be used by the collection tree
  // and thus needs no arena_t::mLock.
  RedBlackTreeNode<arena_t> mLink;

  // Arena id, that we keep away from the beginning of the struct so that
  // free list pointers in TypedBaseAlloc<arena_t> don't overflow in it,
  // and it keeps the value it had after the destructor.
  arena_id_t mId;

  // Operations on this arena require that lock be locked. The MaybeMutex
  // class will elude locking if the arena is accessed from a single thread
  // only (currently only the main thread can be used like this).
  // Can be acquired while holding gArenas.mLock, but must not be acquired or
  // held while holding or acquiring gArenas.mPurgeListLock.
  MaybeMutex mLock MOZ_UNANNOTATED;

  // The lock is required to write to fields of mStats, but it is not needed to
  // read them, so long as inconsistents reads are okay (fields might not make
  // sense together).
  arena_stats_t mStats MOZ_GUARDED_BY(mLock);

  // We can read the allocated counts from mStats without a lock:
  size_t AllocatedBytes() const MOZ_NO_THREAD_SAFETY_ANALYSIS {
    return mStats.allocated_small + mStats.allocated_large;
  }

  // We can read the operations field from mStats without a lock:
  uint64_t Operations() const MOZ_NO_THREAD_SAFETY_ANALYSIS {
    return mStats.operations;
  }

 private:
  // Tree of dirty-page-containing chunks this arena manages.
  RedBlackTree<arena_chunk_t, ArenaDirtyChunkTrait> mChunksDirty
      MOZ_GUARDED_BY(mLock);

#ifdef MALLOC_DOUBLE_PURGE
  // Head of a linked list of MADV_FREE'd-page-containing chunks this
  // arena manages.
  DoublyLinkedList<arena_chunk_t> mChunksMAdvised MOZ_GUARDED_BY(mLock);
#endif

  // In order to avoid rapid chunk allocation/deallocation when an arena
  // oscillates right on the cusp of needing a new chunk, cache the most
  // recently freed chunk.  The spare is left in the arena's chunk trees
  // until it is deleted.
  //
  // There is one spare chunk per arena, rather than one spare total, in
  // order to avoid interactions between multiple threads that could make
  // a single spare inadequate.
  arena_chunk_t* mSpare MOZ_GUARDED_BY(mLock);

  // A per-arena opt-in to randomize the offset of small allocations
  // Needs no lock, read-only.
  bool mRandomizeSmallAllocations;

  // A pseudorandom number generator. Initially null, it gets initialized
  // on first use to avoid recursive malloc initialization (e.g. on OSX
  // arc4random allocates memory).
  mozilla::non_crypto::XorShift128PlusRNG* mPRNG MOZ_GUARDED_BY(mLock);
  bool mIsPRNGInitializing MOZ_GUARDED_BY(mLock);

 public:
  // Whether this is a private arena. Multiple public arenas are just a
  // performance optimization and not a safety feature.
  //
  // Since, for example, we don't want thread-local arenas to grow too much, we
  // use the default arena for bigger allocations. We use this member to allow
  // realloc() to switch out of our arena if needed (which is not allowed for
  // private arenas for security).
  // Needs no lock, read-only.
  bool mIsPrivate;

  // Current count of pages within unused runs that are potentially
  // dirty, and for which madvise(... MADV_FREE) has not been called.  By
  // tracking this, we can institute a limit on how much dirty unused
  // memory is mapped for each arena.
  size_t mNumDirty MOZ_GUARDED_BY(mLock);

  // Precalculated value for faster checks.
  size_t mMaxDirty MOZ_GUARDED_BY(mLock);

  // The current number of pages that are available without a system call (but
  // probably a page fault).
  size_t mNumMAdvised MOZ_GUARDED_BY(mLock);
  size_t mNumFresh MOZ_GUARDED_BY(mLock);

  // Maximum value allowed for mNumDirty.
  // Needs no lock, read-only.
  size_t mMaxDirtyBase;

  // Needs no lock, read-only.
  int32_t mMaxDirtyIncreaseOverride;
  int32_t mMaxDirtyDecreaseOverride;

  // The link to gArenas.mOutstandingPurges.
  // Note that this must only be accessed while holding gArenas.mPurgeListLock
  // (but not arena_t.mLock !) through gArenas.mOutstandingPurges.
  DoublyLinkedListElement<arena_t> mPurgeListElem;

  // A "significant reuse" is when a dirty page is used for a new allocation,
  // it has the CHUNK_MAP_DIRTY bit cleared and CHUNK_MAP_ALLOCATED set.
  //
  // Timestamp of the last time we saw a significant reuse (in ns).
  // Note that this variable is written very often from many threads and read
  // only sparsely on the main thread, but when we read it we need to see the
  // chronologically latest write asap (so we cannot use Relaxed).
  Atomic<uint64_t> mLastSignificantReuseNS;

 public:
  // A flag that indicates if arena will be Purge()'d.
  //
  // It is set either when a thread commits to adding it to mOutstandingPurges
  // or when imitating a Purge.  Cleared only by Purge when we know we are
  // completely done.  This is used to avoid accessing the list (and list lock)
  // on every call to ShouldStartPurge() and to avoid deleting arenas that
  // another thread is purging.
  bool mIsPurgePending MOZ_GUARDED_BY(mLock);

  // A mirror of ArenaCollection::mIsDeferredPurgeEnabled, here only to
  // optimize memory reads in ShouldStartPurge().
  bool mIsDeferredPurgeEnabled MOZ_GUARDED_BY(mLock);

  // True if the arena is in the process of being destroyed, and needs to be
  // released after a concurrent purge completes.
  bool mMustDeleteAfterPurge MOZ_GUARDED_BY(mLock) = false;

  // mLabel describes the label for the firefox profiler.  It's stored in a
  // fixed size area including a null terminating byte.  The actual maximum
  // length of the string is one less than LABEL_MAX_CAPACITY;
  static constexpr size_t LABEL_MAX_CAPACITY = 128;
  char mLabel[LABEL_MAX_CAPACITY];

 private:
  // Size/address-ordered tree of this arena's available runs.  This tree
  // is used for first-best-fit run allocation.
  RedBlackTree<arena_chunk_map_t, ArenaAvailTreeTrait> mRunsAvail
      MOZ_GUARDED_BY(mLock);

 public:
  // mBins is used to store rings of free regions of the following sizes,
  // assuming a 16-byte quantum, 4kB pagesize, and default MALLOC_OPTIONS.
  //
  //  | mBins[i] | size |
  //  +----------+------+
  //  |       0  |    2 |
  //  |       1  |    4 |
  //  |       2  |    8 |
  //  +----------+------+
  //  |       3  |   16 |
  //  |       4  |   32 |
  //  |       5  |   48 |
  //  |       6  |   64 |
  //  |          :      :
  //  |          :      :
  //  |      33  |  496 |
  //  |      34  |  512 |
  //  +----------+------+
  //  |      35  |  768 |
  //  |      36  | 1024 |
  //  |          :      :
  //  |          :      :
  //  |      46  | 3584 |
  //  |      47  | 3840 |
  //  +----------+------+
  arena_bin_t mBins[] MOZ_GUARDED_BY(mLock);  // Dynamically sized.

  explicit arena_t(arena_params_t* aParams, bool aIsPrivate);
  ~arena_t();

  void ResetSmallAllocRandomization();

  void InitPRNG() MOZ_REQUIRES(mLock);

 private:
  void InitChunk(arena_chunk_t* aChunk, size_t aMinCommittedPages)
      MOZ_REQUIRES(mLock);

  // Remove the chunk from the arena.  This removes it from all the page counts.
  // It assumes its run has already been removed and lets the caller clear
  // mSpare as necessary.
  bool RemoveChunk(arena_chunk_t* aChunk) MOZ_REQUIRES(mLock);

  // This may return a chunk that should be destroyed with chunk_dealloc outside
  // of the arena lock.  It is not the same chunk as was passed in (since that
  // chunk now becomes mSpare).
  [[nodiscard]] arena_chunk_t* DemoteChunkToSpare(arena_chunk_t* aChunk)
      MOZ_REQUIRES(mLock);

  // Try to merge the run with its neighbours. Returns the new index of the run
  // (since it may have merged with an earlier one).
  size_t TryCoalesce(arena_chunk_t* aChunk, size_t run_ind, size_t run_pages,
                     size_t size) MOZ_REQUIRES(mLock);

  arena_run_t* AllocRun(size_t aSize, bool aLarge, bool aZero)
      MOZ_REQUIRES(mLock);

  arena_chunk_t* DallocRun(arena_run_t* aRun, bool aDirty) MOZ_REQUIRES(mLock);

  [[nodiscard]] bool SplitRun(arena_run_t* aRun, size_t aSize, bool aLarge,
                              bool aZero) MOZ_REQUIRES(mLock);

  void TrimRunHead(arena_chunk_t* aChunk, arena_run_t* aRun, size_t aOldSize,
                   size_t aNewSize) MOZ_REQUIRES(mLock);

  void TrimRunTail(arena_chunk_t* aChunk, arena_run_t* aRun, size_t aOldSize,
                   size_t aNewSize, bool dirty) MOZ_REQUIRES(mLock);

  arena_run_t* GetNewEmptyBinRun(arena_bin_t* aBin) MOZ_REQUIRES(mLock);

  inline arena_run_t* GetNonFullBinRun(arena_bin_t* aBin) MOZ_REQUIRES(mLock);

  inline uint8_t FindFreeBitInMask(uint32_t aMask, uint32_t& aRng)
      MOZ_REQUIRES(mLock);

  inline void* ArenaRunRegAlloc(arena_run_t* aRun, arena_bin_t* aBin)
      MOZ_REQUIRES(mLock);

  inline void* MallocSmall(size_t aSize, bool aZero) MOZ_EXCLUDES(mLock);

  void* MallocLarge(size_t aSize, bool aZero) MOZ_EXCLUDES(mLock);

  void* MallocHuge(size_t aSize, bool aZero) MOZ_EXCLUDES(mLock);

  void* PallocLarge(size_t aAlignment, size_t aSize, size_t aAllocSize)
      MOZ_EXCLUDES(mLock);

  void* PallocHuge(size_t aSize, size_t aAlignment, bool aZero)
      MOZ_EXCLUDES(mLock);

  void RallocShrinkLarge(arena_chunk_t* aChunk, void* aPtr, size_t aSize,
                         size_t aOldSize) MOZ_EXCLUDES(mLock);

  bool RallocGrowLarge(arena_chunk_t* aChunk, void* aPtr, size_t aSize,
                       size_t aOldSize) MOZ_EXCLUDES(mLock);

  void* RallocSmallOrLarge(void* aPtr, size_t aSize, size_t aOldSize)
      MOZ_EXCLUDES(mLock);

  void* RallocHuge(void* aPtr, size_t aSize, size_t aOldSize)
      MOZ_EXCLUDES(mLock);

 public:
  inline void* Malloc(size_t aSize, bool aZero) MOZ_EXCLUDES(mLock);

  void* Palloc(size_t aAlignment, size_t aSize) MOZ_EXCLUDES(mLock);

  // This may return a chunk that should be destroyed with chunk_dealloc outside
  // of the arena lock.  It is not the same chunk as was passed in (since that
  // chunk now becomes mSpare).
  [[nodiscard]] inline arena_chunk_t* DallocSmall(arena_chunk_t* aChunk,
                                                  void* aPtr,
                                                  arena_chunk_map_t* aMapElm)
      MOZ_REQUIRES(mLock);

  [[nodiscard]] arena_chunk_t* DallocLarge(arena_chunk_t* aChunk, void* aPtr)
      MOZ_REQUIRES(mLock);

  void* Ralloc(void* aPtr, size_t aSize, size_t aOldSize) MOZ_EXCLUDES(mLock);

  void UpdateMaxDirty() MOZ_EXCLUDES(mLock);

#ifdef MALLOC_DECOMMIT
  // During a commit operation (for aReqPages) we have the opportunity of
  // commiting at most aRemPages additional pages.  How many should we commit to
  // amortise system calls?
  size_t ExtraCommitPages(size_t aReqPages, size_t aRemainingPages)
      MOZ_REQUIRES(mLock);
#endif

  // Purge some dirty pages.
  //
  // When this is called the caller has already tested ShouldStartPurge()
  // (possibly on another thread asychronously) or is passing
  // PurgeUnconditional.  However because it's called without the lock it will
  // recheck ShouldContinuePurge() before doing any work.
  //
  // It may purge a number of runs within a single chunk before returning.  It
  // will return Continue if there's more work to do in other chunks
  // (ShouldContinuePurge()).
  //
  // To release more pages from other chunks then it's best to call Purge
  // in a loop, looping when it returns Continue.
  //
  // This must be called without the mLock held (it'll take the lock).
  //
  ArenaPurgeResult Purge(PurgeCondition aCond, PurgeStats& aStats)
      MOZ_EXCLUDES(mLock);

  // Run Purge() in a loop. If sCallback is non-null then collect statistics and
  // publish them through the callback,  aCaller should be used to identify the
  // caller in the profiling data.
  //
  // aCond         - when to stop purging
  // aCaller       - a string representing the caller, this is used for
  //                 profiling
  // aReuseGraceMS - Stop purging the arena if it was used within this many
  //                 milliseconds.  Or 0 to ignore recent reuse.
  // aKeepGoing    - Optional function to implement a time budget.
  //
  ArenaPurgeResult PurgeLoop(
      PurgeCondition aCond, const char* aCaller, uint32_t aReuseGraceMS = 0,
      Maybe<std::function<bool()>> aKeepGoing = Nothing()) MOZ_EXCLUDES(mLock);

  class PurgeInfo {
   private:
    size_t mDirtyInd = 0;
    size_t mDirtyNPages = 0;
    size_t mFreeRunInd = 0;
    size_t mFreeRunLen = 0;

   public:
    arena_t& mArena;

    arena_chunk_t* mChunk = nullptr;

   private:
    PurgeStats& mPurgeStats;

   public:
    size_t FreeRunLenBytes() const { return mFreeRunLen << gPageSize2Pow; }

    // The last index of the free run.
    size_t FreeRunLastInd() const { return mFreeRunInd + mFreeRunLen - 1; }

    void* DirtyPtr() const {
      return (void*)(uintptr_t(mChunk) + (mDirtyInd << gPageSize2Pow));
    }

    size_t DirtyLenBytes() const { return mDirtyNPages << gPageSize2Pow; }

    // Purging memory is seperated into 3 phases.
    //  * FindDirtyPages() which find the dirty pages in a chunk and marks the
    //    run and chunk as busy while holding the lock.
    //  * Release the pages (without the lock)
    //  * UpdatePagesAndCounts() which marks the dirty pages as not-dirty and
    //    updates other counters (while holding the lock).
    //
    // FindDirtyPages() will return false purging should not continue purging in
    // this chunk.  Either because it has no dirty pages or is dying.
    bool FindDirtyPages(bool aPurgedOnce) MOZ_REQUIRES(mArena.mLock);

    // This is used internally by FindDirtyPages to actually perform scanning
    // within a chunk's page tables.
    bool ScanChunkForDirtyPage();

    // Returns a pair, the first field indicates if there are more dirty pages
    // remaining in the current chunk. The second field if non-null points to a
    // chunk that must be released by the caller.
    std::pair<bool, arena_chunk_t*> UpdatePagesAndCounts()
        MOZ_REQUIRES(mArena.mLock);

    // FinishPurgingInChunk() is used whenever we decide to stop purging in a
    // chunk, This could be because there are no more dirty pages, or the chunk
    // is dying, or we hit the arena-level threshold.
    void FinishPurgingInChunk(bool aAddToMAdvised) MOZ_REQUIRES(mArena.mLock);

    explicit PurgeInfo(arena_t& arena, arena_chunk_t* chunk, PurgeStats& stats)
        : mArena(arena), mChunk(chunk), mPurgeStats(stats) {}
  };

  void HardPurge();

  // Check mNumDirty against EffectiveMaxDirty and return the appropriate
  // action to be taken by MayDoOrQueuePurge (outside mLock's scope).
  //
  // None:     Nothing to do.
  // PurgeNow: Immediate synchronous purge.
  // Queue:    Add a new purge request.
  //
  // Note that in the case of deferred purge this function takes into account
  // mIsDeferredPurgeNeeded to avoid useless operations on the purge list
  // that would require gArenas.mPurgeListLock.
  inline purge_action_t ShouldStartPurge() MOZ_REQUIRES(mLock);

  // Take action according to ShouldStartPurge.
  inline void MayDoOrQueuePurge(purge_action_t aAction, const char* aCaller)
      MOZ_EXCLUDES(mLock);

  // Check the EffectiveHalfMaxDirty threshold to decide if we continue purge.
  // This threshold is lower than ShouldStartPurge to have some hysteresis.
  bool ShouldContinuePurge(PurgeCondition aCond) MOZ_REQUIRES(mLock) {
    return (mNumDirty > ((aCond == PurgeUnconditional) ? 0 : mMaxDirty >> 1));
  }

  // Update the last significant reuse timestamp.
  void NotifySignificantReuse() MOZ_EXCLUDES(mLock);

  bool IsMainThreadOnly() const { return !mLock.LockIsEnabled(); }

  void* operator new(size_t aCount) = delete;

  void* operator new(size_t aCount, const fallible_t&) noexcept;

  void operator delete(void*);
};

namespace mozilla {

template <>
struct GetDoublyLinkedListElement<arena_t> {
  static DoublyLinkedListElement<arena_t>& Get(arena_t* aThis) {
    return aThis->mPurgeListElem;
  }
};

}  // namespace mozilla

struct ArenaTreeTrait {
  static RedBlackTreeNode<arena_t>& GetTreeNode(arena_t* aThis) {
    return aThis->mLink;
  }

  static inline Order Compare(arena_t* aNode, arena_t* aOther) {
    MOZ_ASSERT(aNode);
    MOZ_ASSERT(aOther);
    return CompareInt(aNode->mId, aOther->mId);
  }
};

// Bookkeeping for all the arenas used by the allocator.
// Arenas are separated in two categories:
// - "private" arenas, used through the moz_arena_* API
// - all the other arenas: the default arena, and thread-local arenas,
//   used by the standard API.
class ArenaCollection {
 public:
  bool Init() MOZ_REQUIRES(gInitLock) MOZ_EXCLUDES(mLock) {
    MOZ_PUSH_IGNORE_THREAD_SAFETY
    mArenas.Init();
    mPrivateArenas.Init();
#ifndef NON_RANDOM_ARENA_IDS
    mMainThreadArenas.Init();
#endif
    MOZ_POP_THREAD_SAFETY
    arena_params_t params;
    // The main arena allows more dirty pages than the default for other arenas.
    params.mMaxDirty = opt_dirty_max;
    params.mLabel = "Default";
    mDefaultArena =
        mLock.Init() ? CreateArena(/* aIsPrivate = */ false, &params) : nullptr;
    mPurgeListLock.Init();
    mIsDeferredPurgeEnabled = false;
    return bool(mDefaultArena);
  }

  // The requested arena must exist.
  inline arena_t* GetById(arena_id_t aArenaId, bool aIsPrivate)
      MOZ_EXCLUDES(mLock);

  arena_t* CreateArena(bool aIsPrivate, arena_params_t* aParams)
      MOZ_EXCLUDES(mLock);

  void DisposeArena(arena_t* aArena) MOZ_EXCLUDES(mLock) {
    // This will not call MayPurge but only unlink the element in case.
    // It returns true if we successfully removed the item from the list,
    // meaning we have exclusive access to it and can delete it.
    bool delete_now = RemoveFromOutstandingPurges(aArena);

    {
      MutexAutoLock lock(mLock);
      Tree& tree =
#ifndef NON_RANDOM_ARENA_IDS
          aArena->IsMainThreadOnly() ? mMainThreadArenas :
#endif
                                     mPrivateArenas;

      MOZ_RELEASE_ASSERT(tree.Search(aArena), "Arena not in tree");
      tree.Remove(aArena);
      mNumOperationsDisposedArenas += aArena->Operations();
    }
    {
      MutexAutoLock lock(aArena->mLock);
      if (!aArena->mIsPurgePending) {
        // If no purge was pending then we have exclusive access to the
        // arena and must delete it.
        delete_now = true;
      } else if (!delete_now) {
        // The remaining possibility, when we failed to remove the arena from
        // the list (because a purging thread alredy did so) then that thread
        // will be the last thread holding the arena and is now responsible for
        // deleting it.
        aArena->mMustDeleteAfterPurge = true;

        // Not that it's not possible to have checked the list of pending purges
        // BEFORE the arena was added to the list because that would mean that
        // an operation on the arena (free or realloc) was running concurrently
        // with deletion, which would be a memory error and the assertions in
        // the destructor help check for that.
      }
    }

    if (delete_now) {
      delete aArena;
    }
  }

  void SetDefaultMaxDirtyPageModifier(int32_t aModifier) {
    {
      MutexAutoLock lock(mLock);
      mDefaultMaxDirtyPageModifier = aModifier;
      for (auto* arena : iter()) {
        // We can only update max-dirty for main-thread-only arenas from the
        // main thread.
        if (!arena->IsMainThreadOnly() || IsOnMainThreadWeak()) {
          arena->UpdateMaxDirty();
        }
      }
    }
  }

  int32_t DefaultMaxDirtyPageModifier() { return mDefaultMaxDirtyPageModifier; }

  using Tree = RedBlackTree<arena_t, ArenaTreeTrait>;

  struct Iterator : Tree::Iterator {
    explicit Iterator(Tree* aTree, Tree* aSecondTree,
                      Tree* aThirdTree = nullptr)
        : Tree::Iterator(aTree),
          mSecondTree(aSecondTree),
          mThirdTree(aThirdTree) {}

    Item<Iterator> begin() {
      return Item<Iterator>(this, *Tree::Iterator::begin());
    }

    Item<Iterator> end() { return Item<Iterator>(this, nullptr); }

    arena_t* Next() {
      arena_t* result = Tree::Iterator::Next();
      if (!result && mSecondTree) {
        new (this) Iterator(mSecondTree, mThirdTree);
        result = *Tree::Iterator::begin();
      }
      return result;
    }

   private:
    Tree* mSecondTree;
    Tree* mThirdTree;
  };

  Iterator iter() MOZ_REQUIRES(mLock) {
#ifdef NON_RANDOM_ARENA_IDS
    return Iterator(&mArenas, &mPrivateArenas);
#else
    return Iterator(&mArenas, &mPrivateArenas, &mMainThreadArenas);
#endif
  }

  inline arena_t* GetDefault() { return mDefaultArena; }

  // Guards the collection of arenas. Must not be acquired while holding
  // a single arena's lock or mPurgeListLock.
  Mutex mLock MOZ_UNANNOTATED;

  // Guards only the list of outstanding purge requests. Can be acquired
  // while holding gArenas.mLock, but must not be acquired or held while
  // holding or acquiring a single arena's lock.
  Mutex mPurgeListLock;

  // We're running on the main thread which is set by a call to SetMainThread().
  bool IsOnMainThread() const {
    return mMainThreadId.isSome() &&
           ThreadIdEqual(mMainThreadId.value(), GetThreadId());
  }

  // We're running on the main thread or SetMainThread() has never been called.
  bool IsOnMainThreadWeak() const {
    return mMainThreadId.isNothing() || IsOnMainThread();
  }

  // After a fork set the new thread ID in the child.
  // This is done as the first thing after a fork, before mLock even re-inits.
  void ResetMainThread() MOZ_EXCLUDES(mLock) {
    // The post fork handler in the child can run from a MacOS worker thread,
    // so we can't set our main thread to it here.  Instead we have to clear it.
    mMainThreadId = Nothing();
  }

  void SetMainThread() MOZ_EXCLUDES(mLock) {
    MutexAutoLock lock(mLock);
    MOZ_ASSERT(mMainThreadId.isNothing());
    mMainThreadId = Some(GetThreadId());
  }

  // This requires the lock to get a consistent count across all the active
  // + disposed arenas.
  uint64_t OperationsDisposedArenas() MOZ_REQUIRES(mLock) {
    return mNumOperationsDisposedArenas;
  }

  // Enable or disable the lazy purge.
  //
  // Returns the former state of enablement.
  // This is a global setting for all arenas. Changing it may cause an
  // immediate purge for all arenas.
  bool SetDeferredPurge(bool aEnable) {
    MOZ_ASSERT(IsOnMainThreadWeak());

    bool ret = mIsDeferredPurgeEnabled;
    {
      MutexAutoLock lock(mLock);
      mIsDeferredPurgeEnabled = aEnable;
      for (auto* arena : iter()) {
        MaybeMutexAutoLock lock(arena->mLock);
        arena->mIsDeferredPurgeEnabled = aEnable;
      }
    }
    if (ret != aEnable) {
      MayPurgeAll(PurgeIfThreshold, __func__);
    }
    return ret;
  }

  bool IsDeferredPurgeEnabled() { return mIsDeferredPurgeEnabled; }

  // Set aside a new purge request for aArena.
  void AddToOutstandingPurges(arena_t* aArena) MOZ_EXCLUDES(mPurgeListLock);

  // Remove an unhandled purge request for aArena.  Returns true if the arena
  // was in the list.
  bool RemoveFromOutstandingPurges(arena_t* aArena)
      MOZ_EXCLUDES(mPurgeListLock);

  // Execute all outstanding purge requests, if any.
  void MayPurgeAll(PurgeCondition aCond, const char* aCaller);

  // Purge some dirty memory, based on purge requests, returns true if there are
  // more to process.
  //
  // Returns a may_purge_now_result_t with the following meaning:
  // Done:       Purge has completed for all arenas.
  // NeedsMore:  There may be some arenas that needs to be purged now.
  // WantsLater: There is at least one arena that might want a purge later,
  //             according to aReuseGraceMS.
  //
  // Parameters:
  // aPeekOnly:     If true, check only if there is work to do without doing it.
  // aReuseGraceMS: The time to wait with purge after a
  //                significant re-use happened for an arena.
  // aKeepGoing:    If this returns false purging will cease.
  //
  // This could exit for 3 different reasons.
  //  - There are no more requests (it returns false)
  //  - There are more requests but aKeepGoing() returned false. (returns true)
  //  - One arena is completely purged, (returns true).
  //
  may_purge_now_result_t MayPurgeSteps(
      bool aPeekOnly, uint32_t aReuseGraceMS,
      const Maybe<std::function<bool()>>& aKeepGoing);

 private:
  const static arena_id_t MAIN_THREAD_ARENA_BIT = 0x1;

#ifndef NON_RANDOM_ARENA_IDS
  // Can be called with or without lock, depending on aTree.
  inline arena_t* GetByIdInternal(Tree& aTree, arena_id_t aArenaId);

  arena_id_t MakeRandArenaId(bool aIsMainThreadOnly) const MOZ_REQUIRES(mLock);
#endif
  static bool ArenaIdIsMainThreadOnly(arena_id_t aArenaId) {
    return aArenaId & MAIN_THREAD_ARENA_BIT;
  }

  arena_t* mDefaultArena;
  arena_id_t mLastPublicArenaId MOZ_GUARDED_BY(mLock);

  // Accessing mArenas and mPrivateArenas can only be done while holding mLock.
  Tree mArenas MOZ_GUARDED_BY(mLock);
  Tree mPrivateArenas MOZ_GUARDED_BY(mLock);

#ifdef NON_RANDOM_ARENA_IDS
  // Arena ids are pseudo-obfuscated/deobfuscated based on these values randomly
  // initialized on first use.
  arena_id_t mArenaIdKey = 0;
  int8_t mArenaIdRotation = 0;
#else
  // Some mMainThreadArenas accesses to mMainThreadArenas can (and should) elude
  // the lock, see GetById().
  Tree mMainThreadArenas MOZ_GUARDED_BY(mLock);
#endif

  // Set only rarely and then propagated on the same thread to all arenas via
  // UpdateMaxDirty(). But also read in ExtraCommitPages on arbitrary threads.
  // TODO: Could ExtraCommitPages use arena_t::mMaxDirty instead ?
  Atomic<int32_t> mDefaultMaxDirtyPageModifier;
  // This is never changed except for forking, and it does not need mLock.
  Maybe<ThreadId> mMainThreadId;

  // The number of operations that happened in arenas that have since been
  // destroyed.
  uint64_t mNumOperationsDisposedArenas = 0;

  // Linked list of outstanding purges. This list has no particular order.
  // It is ok for an arena to be in this list even if mIsPurgePending is false,
  // it will just cause an extra round of a (most likely no-op) purge.
  // It is not ok to not be in this list but have mIsPurgePending set to true,
  // as this would prevent any future purges for this arena (except for during
  // MayPurgeStep or Purge).
  DoublyLinkedList<arena_t> mOutstandingPurges MOZ_GUARDED_BY(mPurgeListLock);
  // Flag if we should defer purge to later. Only ever set when holding the
  // collection lock. Read only during arena_t ctor.
  Atomic<bool> mIsDeferredPurgeEnabled;
};

MOZ_RUNINIT static ArenaCollection gArenas;

// Protects huge allocation-related data structures.
static Mutex huge_mtx;

// Tree of chunks that are stand-alone huge allocations.
static RedBlackTree<extent_node_t, ExtentTreeTrait> huge
    MOZ_GUARDED_BY(huge_mtx);

// Huge allocation statistics.
static size_t huge_allocated MOZ_GUARDED_BY(huge_mtx);
static size_t huge_mapped MOZ_GUARDED_BY(huge_mtx);
static uint64_t huge_operations MOZ_GUARDED_BY(huge_mtx);

// ******
// Arenas.

// The arena associated with the current thread (per
// jemalloc_thread_local_arena) On OSX, __thread/thread_local circles back
// calling malloc to allocate storage on first access on each thread, which
// leads to an infinite loop, but pthread-based TLS somehow doesn't have this
// problem.
#if !defined(XP_DARWIN)
static MOZ_THREAD_LOCAL(arena_t*) thread_arena;
#else
static detail::ThreadLocal<arena_t*, detail::ThreadLocalKeyStorage>
    thread_arena;
#endif

// ***************************************************************************
// Begin forward declarations.

static void huge_dalloc(void* aPtr, arena_t* aArena);
static bool malloc_init_hard();

#ifndef XP_WIN
#  ifdef XP_DARWIN
#    define FORK_HOOK extern "C"
#  else
#    define FORK_HOOK static
#  endif
FORK_HOOK void _malloc_prefork(void);
FORK_HOOK void _malloc_postfork_parent(void);
FORK_HOOK void _malloc_postfork_child(void);
#  ifdef XP_DARWIN
FORK_HOOK void _malloc_postfork(void);
#  endif
#endif

// End forward declarations.
// ***************************************************************************

// FreeBSD's pthreads implementation calls malloc(3), so the malloc
// implementation has to take pains to avoid infinite recursion during
// initialization.
// Returns whether the allocator was successfully initialized.
static inline bool malloc_init() {
  if (!malloc_initialized) {
    return malloc_init_hard();
  }
  return true;
}

#ifdef ANDROID
// Android's pthread.h does not declare pthread_atfork() until SDK 21.
extern "C" MOZ_EXPORT int pthread_atfork(void (*)(void), void (*)(void),
                                         void (*)(void));
#endif

// ***************************************************************************
// Begin Utility functions/macros.

#ifdef MOZJEMALLOC_PROFILING_CALLBACKS
namespace mozilla {

void jemalloc_set_profiler_callbacks(
    RefPtr<MallocProfilerCallbacks>&& aCallbacks) {
  sCallbacks = aCallbacks;
}

}  // namespace mozilla
#endif

template <>
arena_t* TypedBaseAlloc<arena_t>::sFirstFree = nullptr;

template <>
size_t TypedBaseAlloc<arena_t>::size_of() {
  // Allocate enough space for trailing bins.
  return sizeof(arena_t) + (sizeof(arena_bin_t) * NUM_SMALL_CLASSES);
}

// End Utility functions/macros.
// ***************************************************************************
// Begin arena.

static inline arena_t* thread_local_arena(bool enabled) {
  arena_t* arena;

  if (enabled) {
    // The arena will essentially be leaked if this function is
    // called with `false`, but it doesn't matter at the moment.
    // because in practice nothing actually calls this function
    // with `false`, except maybe at shutdown.
    arena_params_t params;
    params.mLabel = "Thread local";
    arena = gArenas.CreateArena(/* aIsPrivate = */ false, &params);
  } else {
    arena = gArenas.GetDefault();
  }
  thread_arena.set(arena);
  return arena;
}

inline void MozJemalloc::jemalloc_thread_local_arena(bool aEnabled) {
  if (malloc_init()) {
    thread_local_arena(aEnabled);
  }
}

// Choose an arena based on a per-thread value.
static inline arena_t* choose_arena(size_t size) {
  arena_t* ret = nullptr;

  // We can only use TLS if this is a PIC library, since for the static
  // library version, libc's malloc is used by TLS allocation, which
  // introduces a bootstrapping issue.

  if (size > kMaxQuantumClass) {
    // Force the default arena for larger allocations.
    ret = gArenas.GetDefault();
  } else {
    // Check TLS to see if our thread has requested a pinned arena.
    ret = thread_arena.get();
    // If ret is non-null, it must not be in the first page.
    MOZ_DIAGNOSTIC_ASSERT_IF(ret, (size_t)ret >= gPageSize);
    if (!ret) {
      // Nothing in TLS. Pin this thread to the default arena.
      ret = thread_local_arena(false);
    }
  }

  MOZ_DIAGNOSTIC_ASSERT(ret);
  return ret;
}

inline uint8_t arena_t::FindFreeBitInMask(uint32_t aMask, uint32_t& aRng) {
  if (mPRNG != nullptr) {
    if (aRng == UINT_MAX) {
      aRng = mPRNG->next() % 32;
    }
    uint8_t bitIndex;
    // RotateRight asserts when provided bad input.
    aMask = aRng ? RotateRight(aMask, aRng)
                 : aMask;  // Rotate the mask a random number of slots
    bitIndex = CountTrailingZeroes32(aMask);
    return (bitIndex + aRng) % 32;
  }
  return CountTrailingZeroes32(aMask);
}

inline void* arena_t::ArenaRunRegAlloc(arena_run_t* aRun, arena_bin_t* aBin) {
  void* ret;
  unsigned i, mask, bit, regind;
  uint32_t rndPos = UINT_MAX;

  MOZ_DIAGNOSTIC_ASSERT(aRun->mMagic == ARENA_RUN_MAGIC);
  MOZ_ASSERT(aRun->mRegionsMinElement < aBin->mRunNumRegionsMask);

  // Move the first check outside the loop, so that aRun->mRegionsMinElement can
  // be updated unconditionally, without the possibility of updating it
  // multiple times.
  i = aRun->mRegionsMinElement;
  mask = aRun->mRegionsMask[i];
  if (mask != 0) {
    bit = FindFreeBitInMask(mask, rndPos);

    regind = ((i << (LOG2(sizeof(int)) + 3)) + bit);
    MOZ_ASSERT(regind < aBin->mRunNumRegions);
    ret = (void*)(((uintptr_t)aRun) + aBin->mRunFirstRegionOffset +
                  (aBin->mSizeClass * regind));

    // Clear bit.
    mask ^= (1U << bit);
    aRun->mRegionsMask[i] = mask;

    return ret;
  }

  for (i++; i < aBin->mRunNumRegionsMask; i++) {
    mask = aRun->mRegionsMask[i];
    if (mask != 0) {
      bit = FindFreeBitInMask(mask, rndPos);

      regind = ((i << (LOG2(sizeof(int)) + 3)) + bit);
      MOZ_ASSERT(regind < aBin->mRunNumRegions);
      ret = (void*)(((uintptr_t)aRun) + aBin->mRunFirstRegionOffset +
                    (aBin->mSizeClass * regind));

      // Clear bit.
      mask ^= (1U << bit);
      aRun->mRegionsMask[i] = mask;

      // Make a note that nothing before this element
      // contains a free region.
      aRun->mRegionsMinElement = i;  // Low payoff: + (mask == 0);

      return ret;
    }
  }
  // Not reached.
  MOZ_DIAGNOSTIC_ASSERT(0);
  return nullptr;
}

static inline void arena_run_reg_dalloc(arena_run_t* run, arena_bin_t* bin,
                                        void* ptr, size_t size) {
  uint32_t diff, regind;
  unsigned elm, bit;

  MOZ_DIAGNOSTIC_ASSERT(run->mMagic == ARENA_RUN_MAGIC);

  // Avoid doing division with a variable divisor if possible.  Using
  // actual division here can reduce allocator throughput by over 20%!
  diff =
      (uint32_t)((uintptr_t)ptr - (uintptr_t)run - bin->mRunFirstRegionOffset);

  MOZ_ASSERT(diff <=
             (static_cast<unsigned>(bin->mRunSizePages) << gPageSize2Pow));
  regind = diff / bin->mSizeDivisor;

  MOZ_DIAGNOSTIC_ASSERT(diff == regind * size);
  MOZ_DIAGNOSTIC_ASSERT(regind < bin->mRunNumRegions);

  elm = regind >> (LOG2(sizeof(int)) + 3);
  if (elm < run->mRegionsMinElement) {
    run->mRegionsMinElement = elm;
  }
  bit = regind - (elm << (LOG2(sizeof(int)) + 3));
  MOZ_RELEASE_ASSERT((run->mRegionsMask[elm] & (1U << bit)) == 0,
                     "Double-free?");
  run->mRegionsMask[elm] |= (1U << bit);
}

bool arena_t::SplitRun(arena_run_t* aRun, size_t aSize, bool aLarge,
                       bool aZero) {
  arena_chunk_t* chunk = GetChunkForPtr(aRun);
  size_t old_ndirty = chunk->mNumDirty;
  size_t run_ind =
      (unsigned)((uintptr_t(aRun) - uintptr_t(chunk)) >> gPageSize2Pow);
  size_t total_pages =
      (chunk->mPageMap[run_ind].bits & ~gPageSizeMask) >> gPageSize2Pow;
  size_t need_pages = (aSize >> gPageSize2Pow);
  MOZ_ASSERT(need_pages > 0);
  MOZ_ASSERT(need_pages <= total_pages);
  size_t rem_pages = total_pages - need_pages;

  MOZ_ASSERT((chunk->mPageMap[run_ind].bits & CHUNK_MAP_BUSY) == 0);

#ifdef MALLOC_DECOMMIT
  size_t i = 0;
  while (i < need_pages) {
    MOZ_ASSERT((chunk->mPageMap[run_ind + i].bits & CHUNK_MAP_BUSY) == 0);

    // Commit decommitted pages if necessary.  If a decommitted
    // page is encountered, commit all needed adjacent decommitted
    // pages in one operation, in order to reduce system call
    // overhead.
    if (chunk->mPageMap[run_ind + i].bits & CHUNK_MAP_DECOMMITTED) {
      // Advance i+j to just past the index of the last page
      // to commit.  Clear CHUNK_MAP_DECOMMITTED along the way.
      size_t j;
      for (j = 0; i + j < need_pages && (chunk->mPageMap[run_ind + i + j].bits &
                                         CHUNK_MAP_DECOMMITTED);
           j++) {
        // DECOMMITTED, MADVISED and FRESH are mutually exclusive.
        MOZ_ASSERT((chunk->mPageMap[run_ind + i + j].bits &
                    (CHUNK_MAP_FRESH | CHUNK_MAP_MADVISED)) == 0);
      }

      // Consider committing more pages to amortise calls to VirtualAlloc.
      // This only makes sense at the edge of our run hence the if condition
      // here.
      if (i + j == need_pages) {
        size_t extra_commit = ExtraCommitPages(j, rem_pages);
        for (; i + j < need_pages + extra_commit &&
               (chunk->mPageMap[run_ind + i + j].bits &
                CHUNK_MAP_MADVISED_OR_DECOMMITTED);
             j++) {
          MOZ_ASSERT((chunk->mPageMap[run_ind + i + j].bits &
                      (CHUNK_MAP_FRESH | CHUNK_MAP_MADVISED)) == 0);
        }
      }

      if (!pages_commit(
              (void*)(uintptr_t(chunk) + ((run_ind + i) << gPageSize2Pow)),
              j << gPageSize2Pow)) {
        return false;
      }

      // pages_commit zeroes pages, so mark them as such if it succeeded.
      // That's checked further below to avoid manually zeroing the pages.
      for (size_t k = 0; k < j; k++) {
        chunk->mPageMap[run_ind + i + k].bits =
            (chunk->mPageMap[run_ind + i + k].bits & ~CHUNK_MAP_DECOMMITTED) |
            CHUNK_MAP_ZEROED | CHUNK_MAP_FRESH;
      }

      mNumFresh += j;
      i += j;
    } else {
      i++;
    }
  }
#endif

  mRunsAvail.Remove(&chunk->mPageMap[run_ind]);

  // Keep track of trailing unused pages for later use.
  if (rem_pages > 0) {
    chunk->mPageMap[run_ind + need_pages].bits =
        (rem_pages << gPageSize2Pow) |
        (chunk->mPageMap[run_ind + need_pages].bits & gPageSizeMask);
    chunk->mPageMap[run_ind + total_pages - 1].bits =
        (rem_pages << gPageSize2Pow) |
        (chunk->mPageMap[run_ind + total_pages - 1].bits & gPageSizeMask);
    mRunsAvail.Insert(&chunk->mPageMap[run_ind + need_pages]);
  }

  if (chunk->mDirtyRunHint == run_ind) {
    chunk->mDirtyRunHint = run_ind + need_pages;
  }

  for (size_t i = 0; i < need_pages; i++) {
    // Zero if necessary.
    if (aZero) {
      if ((chunk->mPageMap[run_ind + i].bits & CHUNK_MAP_ZEROED) == 0) {
        memset((void*)(uintptr_t(chunk) + ((run_ind + i) << gPageSize2Pow)), 0,
               gPageSize);
        // CHUNK_MAP_ZEROED is cleared below.
      }
    }

    // Update dirty page accounting.
    if (chunk->mPageMap[run_ind + i].bits & CHUNK_MAP_DIRTY) {
      chunk->mNumDirty--;
      mNumDirty--;
      // CHUNK_MAP_DIRTY is cleared below.
    } else if (chunk->mPageMap[run_ind + i].bits & CHUNK_MAP_MADVISED) {
      mStats.committed++;
      mNumMAdvised--;
    }

    if (chunk->mPageMap[run_ind + i].bits & CHUNK_MAP_FRESH) {
      mStats.committed++;
      mNumFresh--;
    }

    // This bit has already been cleared
    MOZ_ASSERT(!(chunk->mPageMap[run_ind + i].bits & CHUNK_MAP_DECOMMITTED));

    // Initialize the chunk map.  This clears the dirty, zeroed and madvised
    // bits, decommitted is cleared above.
    if (aLarge) {
      chunk->mPageMap[run_ind + i].bits = CHUNK_MAP_LARGE | CHUNK_MAP_ALLOCATED;
    } else {
      chunk->mPageMap[run_ind + i].bits = size_t(aRun) | CHUNK_MAP_ALLOCATED;
    }
  }

  // Set the run size only in the first element for large runs.  This is
  // primarily a debugging aid, since the lack of size info for trailing
  // pages only matters if the application tries to operate on an
  // interior pointer.
  if (aLarge) {
    chunk->mPageMap[run_ind].bits |= aSize;
  }

  if (chunk->mNumDirty == 0 && old_ndirty > 0 && !chunk->mIsPurging) {
    mChunksDirty.Remove(chunk);
  }
  return true;
}

void arena_t::InitChunk(arena_chunk_t* aChunk, size_t aMinCommittedPages) {
  new (aChunk) arena_chunk_t(this);

  mStats.mapped += kChunkSize;

  // Setup the chunk's pages in two phases.  First we mark which pages are
  // committed & decommitted and perform the decommit.  Then we update the map
  // to create the runs.

  // Clear the bits for the real header pages.
  size_t i;
  for (i = 0; i < gChunkHeaderNumPages - 1; i++) {
    aChunk->mPageMap[i].bits = 0;
  }
  mStats.committed += gChunkHeaderNumPages - 1;

  // Decommit the last header page (=leading page) as a guard.
  pages_decommit((void*)(uintptr_t(aChunk) + (i << gPageSize2Pow)), gPageSize);
  aChunk->mPageMap[i++].bits = CHUNK_MAP_DECOMMITTED;

  // If MALLOC_DECOMMIT is enabled then commit only the pages we're about to
  // use.  Otherwise commit all of them.
#ifdef MALLOC_DECOMMIT
  size_t n_fresh_pages =
      aMinCommittedPages +
      ExtraCommitPages(
          aMinCommittedPages,
          gChunkNumPages - gChunkHeaderNumPages - aMinCommittedPages - 1);
#else
  size_t n_fresh_pages = gChunkNumPages - 1 - gChunkHeaderNumPages;
#endif

  // The committed pages are marked as Fresh.  Our caller, SplitRun will update
  // this when it uses them.
  for (size_t j = 0; j < n_fresh_pages; j++) {
    aChunk->mPageMap[i + j].bits = CHUNK_MAP_ZEROED | CHUNK_MAP_FRESH;
  }
  i += n_fresh_pages;
  mNumFresh += n_fresh_pages;

#ifndef MALLOC_DECOMMIT
  // If MALLOC_DECOMMIT isn't defined then all the pages are fresh and setup in
  // the loop above.
  MOZ_ASSERT(i == gChunkNumPages - 1);
#endif

  // If MALLOC_DECOMMIT is defined, then this will decommit the remainder of the
  // chunk plus the last page which is a guard page, if it is not defined it
  // will only decommit the guard page.
  pages_decommit((void*)(uintptr_t(aChunk) + (i << gPageSize2Pow)),
                 (gChunkNumPages - i) << gPageSize2Pow);
  for (; i < gChunkNumPages; i++) {
    aChunk->mPageMap[i].bits = CHUNK_MAP_DECOMMITTED;
  }

  // aMinCommittedPages will create a valid run.
  MOZ_ASSERT(aMinCommittedPages > 0);
  MOZ_ASSERT(aMinCommittedPages <= gChunkNumPages - gChunkHeaderNumPages - 1);

  // Create the run.
  aChunk->mPageMap[gChunkHeaderNumPages].bits |= gMaxLargeClass;
  aChunk->mPageMap[gChunkNumPages - 2].bits |= gMaxLargeClass;
  mRunsAvail.Insert(&aChunk->mPageMap[gChunkHeaderNumPages]);
}

bool arena_t::RemoveChunk(arena_chunk_t* aChunk) {
  aChunk->mDying = true;

  // If the chunk has busy pages that means that a Purge() is in progress.
  // We can't remove the chunk now, instead Purge() will do it.
  if (aChunk->mIsPurging) {
    return false;
  }

  if (aChunk->mNumDirty > 0) {
    MOZ_ASSERT(aChunk->mArena == this);
    mChunksDirty.Remove(aChunk);
    mNumDirty -= aChunk->mNumDirty;
    mStats.committed -= aChunk->mNumDirty;
  }

  // Count the number of madvised/fresh pages and update the stats.
  size_t madvised = 0;
  size_t fresh = 0;
  for (size_t i = gChunkHeaderNumPages; i < gChunkNumPages - 1; i++) {
    // There must not be any pages that are not fresh, madvised, decommitted
    // or dirty.
    MOZ_ASSERT(aChunk->mPageMap[i].bits &
               (CHUNK_MAP_FRESH_MADVISED_OR_DECOMMITTED | CHUNK_MAP_DIRTY));
    MOZ_ASSERT((aChunk->mPageMap[i].bits & CHUNK_MAP_BUSY) == 0);

    if (aChunk->mPageMap[i].bits & CHUNK_MAP_MADVISED) {
      madvised++;
    } else if (aChunk->mPageMap[i].bits & CHUNK_MAP_FRESH) {
      fresh++;
    }
  }

  mNumMAdvised -= madvised;
  mNumFresh -= fresh;

#ifdef MALLOC_DOUBLE_PURGE
  if (mChunksMAdvised.ElementProbablyInList(aChunk)) {
    mChunksMAdvised.remove(aChunk);
  }
#endif

  mStats.mapped -= kChunkSize;
  mStats.committed -= gChunkHeaderNumPages - 1;

  return true;
}

arena_chunk_t* arena_t::DemoteChunkToSpare(arena_chunk_t* aChunk) {
  if (mSpare) {
    if (!RemoveChunk(mSpare)) {
      // If we can't remove the spare chunk now purge will finish removing it
      // later.  Set it to null so that the return below will return null and
      // our caller won't delete the chunk before Purge() is finished.
      mSpare = nullptr;
    }
  }

  arena_chunk_t* chunk_dealloc = mSpare;
  mSpare = aChunk;
  return chunk_dealloc;
}

arena_run_t* arena_t::AllocRun(size_t aSize, bool aLarge, bool aZero) {
  arena_run_t* run;
  arena_chunk_map_t* mapelm;
  arena_chunk_map_t key;

  MOZ_ASSERT(aSize <= gMaxLargeClass);
  MOZ_ASSERT((aSize & gPageSizeMask) == 0);

  // Search the arena's chunks for the lowest best fit.
  key.bits = aSize | CHUNK_MAP_KEY;
  mapelm = mRunsAvail.SearchOrNext(&key);
  if (mapelm) {
    arena_chunk_t* chunk = GetChunkForPtr(mapelm);
    size_t pageind = (uintptr_t(mapelm) - uintptr_t(chunk->mPageMap)) /
                     sizeof(arena_chunk_map_t);

    MOZ_ASSERT((chunk->mPageMap[pageind].bits & CHUNK_MAP_BUSY) == 0);
    run = (arena_run_t*)(uintptr_t(chunk) + (pageind << gPageSize2Pow));
  } else if (mSpare && !mSpare->mIsPurging) {
    // Use the spare.
    arena_chunk_t* chunk = mSpare;
    mSpare = nullptr;
    run = (arena_run_t*)(uintptr_t(chunk) +
                         (gChunkHeaderNumPages << gPageSize2Pow));
    // Insert the run into the tree of available runs.
    MOZ_ASSERT((chunk->mPageMap[gChunkHeaderNumPages].bits & CHUNK_MAP_BUSY) ==
               0);
    mRunsAvail.Insert(&chunk->mPageMap[gChunkHeaderNumPages]);
  } else {
    // No usable runs.  Create a new chunk from which to allocate
    // the run.
    arena_chunk_t* chunk =
        (arena_chunk_t*)chunk_alloc(kChunkSize, kChunkSize, false);
    if (!chunk) {
      return nullptr;
    }

    InitChunk(chunk, aSize >> gPageSize2Pow);
    run = (arena_run_t*)(uintptr_t(chunk) +
                         (gChunkHeaderNumPages << gPageSize2Pow));
  }
  // Update page map.
  return SplitRun(run, aSize, aLarge, aZero) ? run : nullptr;
}

void arena_t::UpdateMaxDirty() {
  MaybeMutexAutoLock lock(mLock);
  int32_t modifier = gArenas.DefaultMaxDirtyPageModifier();
  if (modifier) {
    int32_t arenaOverride =
        modifier > 0 ? mMaxDirtyIncreaseOverride : mMaxDirtyDecreaseOverride;
    if (arenaOverride) {
      modifier = arenaOverride;
    }
  }

  mMaxDirty =
      modifier >= 0 ? mMaxDirtyBase << modifier : mMaxDirtyBase >> -modifier;
}

#ifdef MALLOC_DECOMMIT

size_t arena_t::ExtraCommitPages(size_t aReqPages, size_t aRemainingPages) {
  const int32_t modifier = gArenas.DefaultMaxDirtyPageModifier();
  if (modifier < 0) {
    return 0;
  }

  // The maximum size of the page cache
  const size_t max_page_cache = mMaxDirty;

  // The current size of the page cache, note that we use mNumFresh +
  // mNumMAdvised here but Purge() does not.
  const size_t page_cache = mNumDirty + mNumFresh + mNumMAdvised;

  if (page_cache > max_page_cache) {
    // We're already exceeding our dirty page count even though we're trying
    // to allocate.  This can happen due to fragmentation.  Don't commit
    // excess memory since we're probably here due to a larger allocation and
    // small amounts of memory are certainly available in the page cache.
    return 0;
  }
  if (modifier > 0) {
    // If modifier is > 0 then we want to keep all the pages we can, but don't
    // exceed the size of the page cache.  The subtraction cannot underflow
    // because of the condition above.
    return std::min(aRemainingPages, max_page_cache - page_cache);
  }

  // The rest is arbitrary and involves a some assumptions.  I've broken it down
  // into simple expressions to document them more clearly.

  // Assumption 1: a quarter of mMaxDirty is a sensible "minimum
  // target" for the dirty page cache.  Likewise 3 quarters is a sensible
  // "maximum target".  Note that for the maximum we avoid using the whole page
  // cache now so that a free that follows this allocation doesn't immeidatly
  // call Purge (churning memory).
  const size_t min = max_page_cache / 4;
  const size_t max = 3 * max_page_cache / 4;

  // Assumption 2: Committing 32 pages at a time is sufficient to amortise
  // VirtualAlloc costs.
  size_t amortisation_threshold = 32;

  // extra_pages is the number of additional pages needed to meet
  // amortisation_threshold.
  size_t extra_pages = aReqPages < amortisation_threshold
                           ? amortisation_threshold - aReqPages
                           : 0;

  // If committing extra_pages isn't enough to hit the minimum target then
  // increase it.
  if (page_cache + extra_pages < min) {
    extra_pages = min - page_cache;
  } else if (page_cache + extra_pages > max) {
    // If committing extra_pages would exceed our maximum target then it may
    // still be useful to allocate extra pages.  One of the reasons this can
    // happen could be fragmentation of the cache,

    // Therefore reduce the amortisation threshold so that we might allocate
    // some extra pages but avoid exceeding the dirty page cache.
    amortisation_threshold /= 2;
    extra_pages = std::min(aReqPages < amortisation_threshold
                               ? amortisation_threshold - aReqPages
                               : 0,
                           max_page_cache - page_cache);
  }

  // Cap extra_pages to aRemainingPages and adjust aRemainingPages.  We will
  // commit at least this many extra pages.
  extra_pages = std::min(extra_pages, aRemainingPages);

  // Finally if commiting a small number of additional pages now can prevent
  // a small commit later then try to commit a little more now, provided we
  // don't exceed max_page_cache.
  if ((aRemainingPages - extra_pages) < amortisation_threshold / 2 &&
      (page_cache + aRemainingPages) < max_page_cache) {
    return aRemainingPages;
  }

  return extra_pages;
}
#endif

ArenaPurgeResult arena_t::Purge(PurgeCondition aCond, PurgeStats& aStats) {
  arena_chunk_t* chunk;

  // The first critical section will find a chunk and mark dirty pages in it as
  // busy.
  {
    MaybeMutexAutoLock lock(mLock);

    if (mMustDeleteAfterPurge) {
      mIsPurgePending = false;
      return Dying;
    }

#ifdef MOZ_DEBUG
    size_t ndirty = 0;
    for (auto* chunk : mChunksDirty.iter()) {
      ndirty += chunk->mNumDirty;
    }
    // Not all dirty chunks are in mChunksDirty as others might be being Purged.
    MOZ_ASSERT(ndirty <= mNumDirty);
#endif

    if (!ShouldContinuePurge(aCond)) {
      mIsPurgePending = false;
      return ReachedThreshold;
    }

    // Take a single chunk and attempt to purge some of its dirty pages.  The
    // loop below will purge memory from the chunk until either:
    //  * The dirty page count for the arena hits its target,
    //  * Another thread attempts to delete this chunk, or
    //  * The chunk has no more dirty pages.
    // In any of these cases the loop will break and Purge() will return, which
    // means it may return before the arena meets its dirty page count target,
    // the return value is used by the caller to call Purge() again where it
    // will take the next chunk with dirty pages.
    chunk = mChunksDirty.Last();
    if (!chunk) {
      // There are chunks with dirty pages (because mNumDirty > 0 above) but
      // they're not in mChunksDirty.  That can happen if they're busy being
      // purged by other threads.
      // We have to clear the flag to preserve the invariant that if Purge()
      // returns false the flag is clear, if there's more purging work to do in
      // other chunks then either other calls to Purge() (in other threads) will
      // handle it or we rely on ShouldStartPurge() returning true at some point
      // in the future.
      mIsPurgePending = false;
      return Busy;
    }
    MOZ_ASSERT(chunk->mNumDirty > 0);

    // Mark the chunk as busy so it won't be deleted and remove it from
    // mChunksDirty so we're the only thread purging it.
    MOZ_ASSERT(!chunk->mIsPurging);
    mChunksDirty.Remove(chunk);
    chunk->mIsPurging = true;
    aStats.chunks++;
  }  // MaybeMutexAutoLock

  // True if we should continue purging memory from this arena.
  bool continue_purge_arena = true;

  // True if we should continue purging memory in this chunk.
  bool continue_purge_chunk = true;

  // True if at least one Purge operation has occured and therefore we need to
  // call FinishPurgingInChunk() before returning.
  bool purged_once = false;

  while (continue_purge_chunk && continue_purge_arena) {
    // This structure is used to communicate between the two PurgePhase
    // functions.
    PurgeInfo purge_info(*this, chunk, aStats);

    {
      // Phase 1: Find pages that need purging.
      MaybeMutexAutoLock lock(purge_info.mArena.mLock);
      MOZ_ASSERT(chunk->mIsPurging);

      if (purge_info.mArena.mMustDeleteAfterPurge) {
        chunk->mIsPurging = false;
        purge_info.mArena.mIsPurgePending = false;
        return Dying;
      }

      continue_purge_chunk = purge_info.FindDirtyPages(purged_once);
      continue_purge_arena = purge_info.mArena.ShouldContinuePurge(aCond);

      // The code below will exit returning false if these are both false, so
      // clear mIsDeferredPurgeNeeded while we still hold the lock.
      if (!continue_purge_chunk && !continue_purge_arena) {
        purge_info.mArena.mIsPurgePending = false;
      }
    }
    if (!continue_purge_chunk) {
      if (chunk->mDying) {
        // Phase one already unlinked the chunk from structures, we just need to
        // release the memory.
        chunk_dealloc((void*)chunk, kChunkSize, ARENA_CHUNK);
      }
      // There's nothing else to do here, our caller may execute Purge() again
      // if continue_purge_arena is true.
      return continue_purge_arena ? NotDone : ReachedThreshold;
    }

#ifdef MALLOC_DECOMMIT
    pages_decommit(purge_info.DirtyPtr(), purge_info.DirtyLenBytes());
#else
#  ifdef XP_SOLARIS
    posix_madvise(purge_info.DirtyPtr(), purge_info.DirtyLenBytes(), MADV_FREE);
#  else
    madvise(purge_info.DirtyPtr(), purge_info.DirtyLenBytes(), MADV_FREE);
#  endif
#endif

    arena_chunk_t* chunk_to_release = nullptr;
    bool is_dying;
    {
      // Phase 2: Mark the pages with their final state (madvised or
      // decommitted) and fix up any other bookkeeping.
      MaybeMutexAutoLock lock(purge_info.mArena.mLock);
      MOZ_ASSERT(chunk->mIsPurging);

      // We can't early exit if the arena is dying, we have to finish the purge
      // (which restores the state so the destructor will check it) and maybe
      // release the old spare arena.
      is_dying = purge_info.mArena.mMustDeleteAfterPurge;

      auto [cpc, ctr] = purge_info.UpdatePagesAndCounts();
      continue_purge_chunk = cpc;
      chunk_to_release = ctr;
      continue_purge_arena = purge_info.mArena.ShouldContinuePurge(aCond);

      if (!continue_purge_chunk || !continue_purge_arena) {
        // We're going to stop purging here so update the chunk's bookkeeping.
        purge_info.FinishPurgingInChunk(true);
        purge_info.mArena.mIsPurgePending = false;
      }
    }  // MaybeMutexAutoLock

    // Phase 2 can release the spare chunk (not always == chunk) so an extra
    // parameter is used to return that chunk.
    if (chunk_to_release) {
      chunk_dealloc((void*)chunk_to_release, kChunkSize, ARENA_CHUNK);
    }
    if (is_dying) {
      return Dying;
    }
    purged_once = true;
  }

  return continue_purge_arena ? NotDone : ReachedThreshold;
}

ArenaPurgeResult arena_t::PurgeLoop(PurgeCondition aCond, const char* aCaller,
                                    uint32_t aReuseGraceMS,
                                    Maybe<std::function<bool()>> aKeepGoing) {
  PurgeStats purge_stats(mId, mLabel, aCaller);

#ifdef MOZJEMALLOC_PROFILING_CALLBACKS
  // We hold our own reference to callbacks for the duration of PurgeLoop to
  // make sure it's not released during purging.
  RefPtr<MallocProfilerCallbacks> callbacks = sCallbacks;
  TimeStamp start;
  if (callbacks) {
    start = TimeStamp::Now();
  }
#endif

  uint64_t reuseGraceNS = (uint64_t)aReuseGraceMS * 1000 * 1000;
  uint64_t now = aReuseGraceMS ? 0 : GetTimestampNS();
  ArenaPurgeResult pr;
  do {
    pr = Purge(aCond, purge_stats);
    now = aReuseGraceMS ? 0 : GetTimestampNS();
  } while (
      pr == NotDone &&
      (!aReuseGraceMS || (now - mLastSignificantReuseNS >= reuseGraceNS)) &&
      (!aKeepGoing || (*aKeepGoing)()));

#ifdef MOZJEMALLOC_PROFILING_CALLBACKS
  if (callbacks) {
    TimeStamp end = TimeStamp::Now();
    // We can't hold an arena lock while committing profiler markers.
    callbacks->OnPurge(start, end, purge_stats, pr);
  }
#endif

  return pr;
}

bool arena_t::PurgeInfo::FindDirtyPages(bool aPurgedOnce) {
  // It's possible that the previously dirty pages have now been
  // allocated or the chunk is dying.
  if (mChunk->mNumDirty == 0 || mChunk->mDying) {
    // Add the chunk to the mChunksMAdvised list if it's had at least one
    // madvise.
    FinishPurgingInChunk(aPurgedOnce);
    return false;
  }

  MOZ_ALWAYS_TRUE(ScanChunkForDirtyPage());
  MOZ_ASSERT(mDirtyInd != 0);
  MOZ_ASSERT(mFreeRunInd >= gChunkHeaderNumPages);
  MOZ_ASSERT(mFreeRunInd <= mDirtyInd);
  MOZ_ASSERT(mFreeRunLen > 0);

  // Look for the next not-dirty page, it could be the guard page at the end
  // of the chunk.
  for (size_t i = 0; mDirtyInd + i < gChunkNumPages; i++) {
    size_t& bits = mChunk->mPageMap[mDirtyInd + i].bits;

    // We must not find any busy pages because this chunk shouldn't be in the
    // dirty list.
    MOZ_ASSERT(!(bits & CHUNK_MAP_BUSY));

    if (!(bits & CHUNK_MAP_DIRTY)) {
      mDirtyNPages = i;
      break;
    }
    MOZ_ASSERT((bits & CHUNK_MAP_FRESH_MADVISED_OR_DECOMMITTED) == 0);
    bits ^= CHUNK_MAP_DIRTY;
  }
  MOZ_ASSERT(mDirtyNPages > 0);
  MOZ_ASSERT(mDirtyNPages <= mChunk->mNumDirty);
  MOZ_ASSERT(mFreeRunInd + mFreeRunLen >= mDirtyInd + mDirtyNPages);

  // Mark the run as busy so that another thread freeing memory won't try to
  // coalesce it.
  mChunk->mPageMap[mFreeRunInd].bits |= CHUNK_MAP_BUSY;
  mChunk->mPageMap[FreeRunLastInd()].bits |= CHUNK_MAP_BUSY;

  mChunk->mNumDirty -= mDirtyNPages;
  mArena.mNumDirty -= mDirtyNPages;

  // Before we unlock ensure that no other thread can allocate from these
  // pages.
  if (mArena.mSpare != mChunk) {
    mArena.mRunsAvail.Remove(&mChunk->mPageMap[mFreeRunInd]);
  }
  return true;
}

// Look for the first dirty page and the run it belongs to.
bool arena_t::PurgeInfo::ScanChunkForDirtyPage() {
  // Scan in two nested loops.  The outer loop iterates over runs, and the inner
  // loop iterates over pages within unallocated runs.
  size_t run_pages;
  for (size_t run_idx = mChunk->mDirtyRunHint; run_idx < gChunkNumPages;
       run_idx += run_pages) {
    size_t run_bits = mChunk->mPageMap[run_idx].bits;
    // We must not find any busy pages because this chunk shouldn't be in
    // the dirty list.
    MOZ_ASSERT((run_bits & CHUNK_MAP_BUSY) == 0);

    // Determine the run's size, this is used in the loop iteration to move to
    // the next run.
    if (run_bits & CHUNK_MAP_LARGE || !(run_bits & CHUNK_MAP_ALLOCATED)) {
      size_t size = run_bits & ~gPageSizeMask;
      run_pages = size >> gPageSize2Pow;
    } else {
      arena_run_t* run =
          reinterpret_cast<arena_run_t*>(run_bits & ~gPageSizeMask);
      MOZ_ASSERT(run == reinterpret_cast<arena_run_t*>(
                            reinterpret_cast<uintptr_t>(mChunk) +
                            (run_idx << gPageSize2Pow)));
      run_pages = run->mBin->mRunSizePages;
    }
    MOZ_ASSERT(run_pages > 0);
    MOZ_ASSERT(run_idx + run_pages <= gChunkNumPages);

    if (run_bits & CHUNK_MAP_ALLOCATED) {
      // Allocated runs won't contain dirty pages.
      continue;
    }

    mFreeRunInd = run_idx;
    mFreeRunLen = run_pages;

    // Scan for dirty pages.
    for (size_t page_idx = run_idx; page_idx < run_idx + run_pages;
         page_idx++) {
      size_t page_bits = mChunk->mPageMap[page_idx].bits;
      // We must not find any busy pages because this chunk shouldn't be in
      // the dirty list.
      MOZ_ASSERT((page_bits & CHUNK_MAP_BUSY) == 0);

      if (page_bits & CHUNK_MAP_DIRTY) {
        MOZ_ASSERT((page_bits & CHUNK_MAP_FRESH_MADVISED_OR_DECOMMITTED) == 0);
        mDirtyInd = page_idx;
        mChunk->mDirtyRunHint = run_idx;
        return true;
      }
    }
  }

  return false;
}

std::pair<bool, arena_chunk_t*> arena_t::PurgeInfo::UpdatePagesAndCounts() {
  for (size_t i = 0; i < mDirtyNPages; i++) {
    // The page must not have any of the madvised, decommited or dirty bits
    // set.
    MOZ_ASSERT((mChunk->mPageMap[mDirtyInd + i].bits &
                (CHUNK_MAP_FRESH_MADVISED_OR_DECOMMITTED | CHUNK_MAP_DIRTY)) ==
               0);
#ifdef MALLOC_DECOMMIT
    const size_t free_operation = CHUNK_MAP_DECOMMITTED;
#else
    const size_t free_operation = CHUNK_MAP_MADVISED;
#endif
    mChunk->mPageMap[mDirtyInd + i].bits ^= free_operation;
  }

  // Remove the CHUNK_MAP_BUSY marks from the run.
#ifdef MOZ_DEBUG
  MOZ_ASSERT(mChunk->mPageMap[mFreeRunInd].bits & CHUNK_MAP_BUSY);
  MOZ_ASSERT(mChunk->mPageMap[FreeRunLastInd()].bits & CHUNK_MAP_BUSY);
#endif
  mChunk->mPageMap[mFreeRunInd].bits &= ~CHUNK_MAP_BUSY;
  mChunk->mPageMap[FreeRunLastInd()].bits &= ~CHUNK_MAP_BUSY;

#ifndef MALLOC_DECOMMIT
  mArena.mNumMAdvised += mDirtyNPages;
#endif

  mArena.mStats.committed -= mDirtyNPages;
  mPurgeStats.pages += mDirtyNPages;
  mPurgeStats.system_calls++;

  // Note that this code can't update the dirty run hint.  There may be other
  // dirty pages within the same run.

  if (mChunk->mDying) {
    // A dying chunk doesn't need to be coaleased, it will already have one
    // large run.
    MOZ_ASSERT(mFreeRunInd == gChunkHeaderNumPages &&
               mFreeRunLen == gChunkNumPages - gChunkHeaderNumPages - 1);

    return std::make_pair(false, mChunk);
  }

  bool was_empty = mChunk->IsEmpty();
  mFreeRunInd =
      mArena.TryCoalesce(mChunk, mFreeRunInd, mFreeRunLen, FreeRunLenBytes());

  arena_chunk_t* chunk_to_release = nullptr;
  if (!was_empty && mChunk->IsEmpty()) {
    // This now-empty chunk will become the spare chunk and the spare
    // chunk will be returned for deletion.
    chunk_to_release = mArena.DemoteChunkToSpare(mChunk);
  }

  if (mChunk != mArena.mSpare) {
    mArena.mRunsAvail.Insert(&mChunk->mPageMap[mFreeRunInd]);
  }

  return std::make_pair(mChunk->mNumDirty != 0, chunk_to_release);
}

void arena_t::PurgeInfo::FinishPurgingInChunk(bool aAddToMAdvised) {
  // If there's no more purge activity for this chunk then finish up while
  // we still have the lock.
  MOZ_ASSERT(mChunk->mIsPurging);
  mChunk->mIsPurging = false;

  if (mChunk->mDying) {
    // Another thread tried to delete this chunk while we weren't holding
    // the lock.  Now it's our responsibility to finish deleting it.  First
    // clear its dirty pages so that RemoveChunk() doesn't try to remove it
    // from mChunksDirty because it won't be there.
    mArena.mNumDirty -= mChunk->mNumDirty;
    mArena.mStats.committed -= mChunk->mNumDirty;
    mChunk->mNumDirty = 0;

    DebugOnly<bool> release_chunk = mArena.RemoveChunk(mChunk);
    // RemoveChunk() can't return false because mIsPurging was false
    // during the call.
    MOZ_ASSERT(release_chunk);
    return;
  }

  if (mChunk->mNumDirty != 0) {
    mArena.mChunksDirty.Insert(mChunk);
  }

#ifdef MALLOC_DOUBLE_PURGE
  if (aAddToMAdvised) {
    // The chunk might already be in the list, but this
    // makes sure it's at the front.
    if (mArena.mChunksMAdvised.ElementProbablyInList(mChunk)) {
      mArena.mChunksMAdvised.remove(mChunk);
    }
    mArena.mChunksMAdvised.pushFront(mChunk);
  }
#endif
}

// run_pages and size make each-other redundant. But we use them both and the
// caller computes both so this function requires both and will assert if they
// are inconsistent.
size_t arena_t::TryCoalesce(arena_chunk_t* aChunk, size_t run_ind,
                            size_t run_pages, size_t size) {
  // Copy in/out parameters to local variables so that we don't need '*'
  // operators throughout this code but also so that type checking is stricter
  // (references are too easily coerced).
  MOZ_ASSERT(size == run_pages << gPageSize2Pow);

  // Try to coalesce forward.
  if (run_ind + run_pages < gChunkNumPages - 1 &&
      (aChunk->mPageMap[run_ind + run_pages].bits &
       (CHUNK_MAP_ALLOCATED | CHUNK_MAP_BUSY)) == 0) {
    size_t nrun_size =
        aChunk->mPageMap[run_ind + run_pages].bits & ~gPageSizeMask;

    // Remove successor from tree of available runs; the coalesced run is
    // inserted later.
    mRunsAvail.Remove(&aChunk->mPageMap[run_ind + run_pages]);

    size += nrun_size;
    run_pages = size >> gPageSize2Pow;

    MOZ_DIAGNOSTIC_ASSERT((aChunk->mPageMap[run_ind + run_pages - 1].bits &
                           ~gPageSizeMask) == nrun_size);
    aChunk->mPageMap[run_ind].bits =
        size | (aChunk->mPageMap[run_ind].bits & gPageSizeMask);
    aChunk->mPageMap[run_ind + run_pages - 1].bits =
        size | (aChunk->mPageMap[run_ind + run_pages - 1].bits & gPageSizeMask);
  }

  // Try to coalesce backward.
  if (run_ind > gChunkHeaderNumPages &&
      (aChunk->mPageMap[run_ind - 1].bits &
       (CHUNK_MAP_ALLOCATED | CHUNK_MAP_BUSY)) == 0) {
    size_t prun_size = aChunk->mPageMap[run_ind - 1].bits & ~gPageSizeMask;

    run_ind -= prun_size >> gPageSize2Pow;

    // Remove predecessor from tree of available runs; the coalesced run is
    // inserted later.
    mRunsAvail.Remove(&aChunk->mPageMap[run_ind]);

    size += prun_size;
    run_pages = size >> gPageSize2Pow;

    MOZ_DIAGNOSTIC_ASSERT((aChunk->mPageMap[run_ind].bits & ~gPageSizeMask) ==
                          prun_size);
    aChunk->mPageMap[run_ind].bits =
        size | (aChunk->mPageMap[run_ind].bits & gPageSizeMask);
    aChunk->mPageMap[run_ind + run_pages - 1].bits =
        size | (aChunk->mPageMap[run_ind + run_pages - 1].bits & gPageSizeMask);
  }

  // If the dirty run hint points within the run then the new greater run
  // is the run with the lowest index containing dirty pages.  So update the
  // hint.
  if ((aChunk->mDirtyRunHint > run_ind) &&
      (aChunk->mDirtyRunHint < run_ind + run_pages)) {
    aChunk->mDirtyRunHint = run_ind;
  }

  return run_ind;
}

arena_chunk_t* arena_t::DallocRun(arena_run_t* aRun, bool aDirty) {
  arena_chunk_t* chunk = GetChunkForPtr(aRun);
  size_t run_ind =
      (size_t)((uintptr_t(aRun) - uintptr_t(chunk)) >> gPageSize2Pow);
  MOZ_DIAGNOSTIC_ASSERT(run_ind >= gChunkHeaderNumPages);
  MOZ_RELEASE_ASSERT(run_ind < gChunkNumPages - 1);

  size_t size, run_pages;
  if ((chunk->mPageMap[run_ind].bits & CHUNK_MAP_LARGE) != 0) {
    size = chunk->mPageMap[run_ind].bits & ~gPageSizeMask;
    run_pages = (size >> gPageSize2Pow);
  } else {
    run_pages = aRun->mBin->mRunSizePages;
    size = run_pages << gPageSize2Pow;
  }

  // Mark pages as unallocated in the chunk map.
  if (aDirty) {
    for (size_t i = 0; i < run_pages; i++) {
      MOZ_DIAGNOSTIC_ASSERT(
          (chunk->mPageMap[run_ind + i].bits & CHUNK_MAP_DIRTY) == 0);
      chunk->mPageMap[run_ind + i].bits = CHUNK_MAP_DIRTY;
    }

    if (chunk->mNumDirty == 0 && !chunk->mIsPurging) {
      mChunksDirty.Insert(chunk);
    }
    chunk->mNumDirty += run_pages;
    mNumDirty += run_pages;
  } else {
    for (size_t i = 0; i < run_pages; i++) {
      chunk->mPageMap[run_ind + i].bits &=
          ~(CHUNK_MAP_LARGE | CHUNK_MAP_ALLOCATED);
    }
  }
  chunk->mPageMap[run_ind].bits =
      size | (chunk->mPageMap[run_ind].bits & gPageSizeMask);
  chunk->mPageMap[run_ind + run_pages - 1].bits =
      size | (chunk->mPageMap[run_ind + run_pages - 1].bits & gPageSizeMask);

  run_ind = TryCoalesce(chunk, run_ind, run_pages, size);

  // Now that run_ind is finalised we can update the dirty run hint.
  if (aDirty && run_ind < chunk->mDirtyRunHint) {
    chunk->mDirtyRunHint = run_ind;
  }

  // Deallocate chunk if it is now completely unused.
  arena_chunk_t* chunk_dealloc = nullptr;
  if (chunk->IsEmpty()) {
    chunk_dealloc = DemoteChunkToSpare(chunk);
  } else {
    // Insert into tree of available runs, now that coalescing is complete.
    mRunsAvail.Insert(&chunk->mPageMap[run_ind]);
  }

  return chunk_dealloc;
}

void arena_t::TrimRunHead(arena_chunk_t* aChunk, arena_run_t* aRun,
                          size_t aOldSize, size_t aNewSize) {
  size_t pageind = (uintptr_t(aRun) - uintptr_t(aChunk)) >> gPageSize2Pow;
  size_t head_npages = (aOldSize - aNewSize) >> gPageSize2Pow;

  MOZ_ASSERT(aOldSize > aNewSize);

  // Update the chunk map so that arena_t::RunDalloc() can treat the
  // leading run as separately allocated.
  aChunk->mPageMap[pageind].bits =
      (aOldSize - aNewSize) | CHUNK_MAP_LARGE | CHUNK_MAP_ALLOCATED;
  aChunk->mPageMap[pageind + head_npages].bits =
      aNewSize | CHUNK_MAP_LARGE | CHUNK_MAP_ALLOCATED;

  DebugOnly<arena_chunk_t*> no_chunk = DallocRun(aRun, false);
  // This will never release a chunk as there's still at least one allocated
  // run.
  MOZ_ASSERT(!no_chunk);
}

void arena_t::TrimRunTail(arena_chunk_t* aChunk, arena_run_t* aRun,
                          size_t aOldSize, size_t aNewSize, bool aDirty) {
  size_t pageind = (uintptr_t(aRun) - uintptr_t(aChunk)) >> gPageSize2Pow;
  size_t npages = aNewSize >> gPageSize2Pow;

  MOZ_ASSERT(aOldSize > aNewSize);

  // Update the chunk map so that arena_t::RunDalloc() can treat the
  // trailing run as separately allocated.
  aChunk->mPageMap[pageind].bits =
      aNewSize | CHUNK_MAP_LARGE | CHUNK_MAP_ALLOCATED;
  aChunk->mPageMap[pageind + npages].bits =
      (aOldSize - aNewSize) | CHUNK_MAP_LARGE | CHUNK_MAP_ALLOCATED;

  DebugOnly<arena_chunk_t*> no_chunk =
      DallocRun((arena_run_t*)(uintptr_t(aRun) + aNewSize), aDirty);

  // This will never release a chunk as there's still at least one allocated
  // run.
  MOZ_ASSERT(!no_chunk);
}

arena_run_t* arena_t::GetNewEmptyBinRun(arena_bin_t* aBin) {
  arena_run_t* run;
  unsigned i, remainder;

  // Allocate a new run.
  run = AllocRun(static_cast<size_t>(aBin->mRunSizePages) << gPageSize2Pow,
                 false, false);
  if (!run) {
    return nullptr;
  }

  // Initialize run internals.
  run->mBin = aBin;

  for (i = 0; i < aBin->mRunNumRegionsMask - 1; i++) {
    run->mRegionsMask[i] = UINT_MAX;
  }
  remainder = aBin->mRunNumRegions & ((1U << (LOG2(sizeof(int)) + 3)) - 1);
  if (remainder == 0) {
    run->mRegionsMask[i] = UINT_MAX;
  } else {
    // The last element has spare bits that need to be unset.
    run->mRegionsMask[i] =
        (UINT_MAX >> ((1U << (LOG2(sizeof(int)) + 3)) - remainder));
  }

  run->mRegionsMinElement = 0;

  run->mNumFree = aBin->mRunNumRegions;
#if defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
  run->mMagic = ARENA_RUN_MAGIC;
#endif

  // Make sure we continue to use this run for subsequent allocations.
  new (&run->mRunListElem) DoublyLinkedListElement<arena_run_t>();
  aBin->mNonFullRuns.pushFront(run);

  aBin->mNumRuns++;
  return run;
}

arena_run_t* arena_t::GetNonFullBinRun(arena_bin_t* aBin) {
  auto mrf_head = aBin->mNonFullRuns.begin();
  if (mrf_head) {
    // Take the head and if we are going to fill it, remove it from our list.
    arena_run_t* run = &(*mrf_head);
    MOZ_DIAGNOSTIC_ASSERT(run->mMagic == ARENA_RUN_MAGIC);
    if (run->mNumFree == 1) {
      aBin->mNonFullRuns.remove(run);
    }
    return run;
  }
  return GetNewEmptyBinRun(aBin);
}

void arena_bin_t::Init(SizeClass aSizeClass) {
  size_t try_run_size;
  unsigned try_nregs, try_mask_nelms, try_reg0_offset;
  // Size of the run header, excluding mRegionsMask.
  static const size_t kFixedHeaderSize = offsetof(arena_run_t, mRegionsMask);

  MOZ_ASSERT(aSizeClass.Size() <= gMaxBinClass);

  try_run_size = gPageSize;

  mSizeClass = aSizeClass.Size();
  mNumRuns = 0;

  // Run size expansion loop.
  while (true) {
    try_nregs = ((try_run_size - kFixedHeaderSize) / mSizeClass) +
                1;  // Counter-act try_nregs-- in loop.

    // The do..while loop iteratively reduces the number of regions until
    // the run header and the regions no longer overlap.  A closed formula
    // would be quite messy, since there is an interdependency between the
    // header's mask length and the number of regions.
    do {
      try_nregs--;
      try_mask_nelms =
          (try_nregs >> (LOG2(sizeof(int)) + 3)) +
          ((try_nregs & ((1U << (LOG2(sizeof(int)) + 3)) - 1)) ? 1 : 0);
      try_reg0_offset = try_run_size - (try_nregs * mSizeClass);
    } while (kFixedHeaderSize + (sizeof(unsigned) * try_mask_nelms) >
             try_reg0_offset);

    // Try to keep the run overhead below kRunOverhead.
    if (Fraction(try_reg0_offset, try_run_size) <= kRunOverhead) {
      break;
    }

    // If the overhead is larger than the size class, it means the size class
    // is small and doesn't align very well with the header. It's desirable to
    // have smaller run sizes for them, so relax the overhead requirement.
    if (try_reg0_offset > mSizeClass) {
      if (Fraction(try_reg0_offset, try_run_size) <= kRunRelaxedOverhead) {
        break;
      }
    }

    // The run header includes one bit per region of the given size. For sizes
    // small enough, the number of regions is large enough that growing the run
    // size barely moves the needle for the overhead because of all those bits.
    // For example, for a size of 8 bytes, adding 4KiB to the run size adds
    // close to 512 bits to the header, which is 64 bytes.
    // With such overhead, there is no way to get to the wanted overhead above,
    // so we give up if the required size for mRegionsMask more than doubles the
    // size of the run header.
    if (try_mask_nelms * sizeof(unsigned) >= kFixedHeaderSize) {
      break;
    }

    // If next iteration is going to be larger than the largest possible large
    // size class, then we didn't find a setup where the overhead is small
    // enough, and we can't do better than the current settings, so just use
    // that.
    if (try_run_size + gPageSize > gMaxLargeClass) {
      break;
    }

    // Try more aggressive settings.
    try_run_size += gPageSize;
  }

  MOZ_ASSERT(kFixedHeaderSize + (sizeof(unsigned) * try_mask_nelms) <=
             try_reg0_offset);
  MOZ_ASSERT((try_mask_nelms << (LOG2(sizeof(int)) + 3)) >= try_nregs);

  // Our list management would break if mRunNumRegions == 1 and we should use
  // a large size class instead, anyways.
  MOZ_ASSERT(try_nregs > 1);

  // Copy final settings.
  MOZ_ASSERT((try_run_size >> gPageSize2Pow) <= UINT8_MAX);
  mRunSizePages = static_cast<uint8_t>(try_run_size >> gPageSize2Pow);
  mRunNumRegions = try_nregs;
  mRunNumRegionsMask = try_mask_nelms;
  mRunFirstRegionOffset = try_reg0_offset;
  mSizeDivisor = FastDivisor<uint16_t>(aSizeClass.Size(), try_run_size);
}

void arena_t::ResetSmallAllocRandomization() {
  if (MOZ_UNLIKELY(opt_randomize_small)) {
    MaybeMutexAutoLock lock(mLock);
    InitPRNG();
  }
  mRandomizeSmallAllocations = opt_randomize_small;
}

void arena_t::InitPRNG() {
  // Both another thread could race and the code backing RandomUint64
  // (arc4random for example) may allocate memory while here, so we must
  // ensure to start the mPRNG initialization only once and to not hold
  // the lock while initializing.
  mIsPRNGInitializing = true;
  {
    mLock.Unlock();
    mozilla::Maybe<uint64_t> prngState1 = mozilla::RandomUint64();
    mozilla::Maybe<uint64_t> prngState2 = mozilla::RandomUint64();
    mLock.Lock();

    mozilla::non_crypto::XorShift128PlusRNG prng(prngState1.valueOr(0),
                                                 prngState2.valueOr(0));
    if (mPRNG) {
      *mPRNG = prng;
    } else {
      void* backing =
          base_alloc(sizeof(mozilla::non_crypto::XorShift128PlusRNG));
      mPRNG = new (backing)
          mozilla::non_crypto::XorShift128PlusRNG(std::move(prng));
    }
  }
  mIsPRNGInitializing = false;
}

void* arena_t::MallocSmall(size_t aSize, bool aZero) {
  void* ret;
  arena_bin_t* bin;
  arena_run_t* run;
  SizeClass sizeClass(aSize);
  aSize = sizeClass.Size();

  switch (sizeClass.Type()) {
    case SizeClass::Tiny:
      bin = &mBins[FloorLog2(aSize / kMinTinyClass)];
      break;
    case SizeClass::Quantum:
      // Although we divide 2 things by kQuantum, the compiler will
      // reduce `kMinQuantumClass / kQuantum` and `kNumTinyClasses` to a
      // single constant.
      bin = &mBins[kNumTinyClasses + (aSize / kQuantum) -
                   (kMinQuantumClass / kQuantum)];
      break;
    case SizeClass::QuantumWide:
      bin =
          &mBins[kNumTinyClasses + kNumQuantumClasses + (aSize / kQuantumWide) -
                 (kMinQuantumWideClass / kQuantumWide)];
      break;
    case SizeClass::SubPage:
      bin =
          &mBins[kNumTinyClasses + kNumQuantumClasses + kNumQuantumWideClasses +
                 (FloorLog2(aSize) - LOG2(kMinSubPageClass))];
      break;
    default:
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Unexpected size class type");
  }
  MOZ_DIAGNOSTIC_ASSERT(aSize == bin->mSizeClass);

  size_t num_dirty_before, num_dirty_after;
  {
    MaybeMutexAutoLock lock(mLock);

#ifdef MOZ_DEBUG
    bool isInitializingThread(false);
#endif

    if (MOZ_UNLIKELY(mRandomizeSmallAllocations && mPRNG == nullptr &&
                     !mIsPRNGInitializing)) {
#ifdef MOZ_DEBUG
      isInitializingThread = true;
#endif
      InitPRNG();
    }

    MOZ_ASSERT(!mRandomizeSmallAllocations || mPRNG ||
               (mIsPRNGInitializing && !isInitializingThread));

    num_dirty_before = mNumDirty;
    run = GetNonFullBinRun(bin);
    num_dirty_after = mNumDirty;
    if (MOZ_UNLIKELY(!run)) {
      return nullptr;
    }
    MOZ_DIAGNOSTIC_ASSERT(run->mMagic == ARENA_RUN_MAGIC);
    MOZ_DIAGNOSTIC_ASSERT(run->mNumFree > 0);
    ret = ArenaRunRegAlloc(run, bin);
    MOZ_DIAGNOSTIC_ASSERT(ret);
    run->mNumFree--;
    if (!ret) {
      return nullptr;
    }

    mStats.allocated_small += aSize;
    mStats.operations++;
  }
  if (num_dirty_after < num_dirty_before) {
    NotifySignificantReuse();
  }
  if (!aZero) {
    ApplyZeroOrJunk(ret, aSize);
  } else {
    memset(ret, 0, aSize);
  }

  return ret;
}

void* arena_t::MallocLarge(size_t aSize, bool aZero) {
  void* ret;

  // Large allocation.
  aSize = PAGE_CEILING(aSize);

  size_t num_dirty_before, num_dirty_after;
  {
    MaybeMutexAutoLock lock(mLock);
    num_dirty_before = mNumDirty;
    ret = AllocRun(aSize, true, aZero);
    num_dirty_after = mNumDirty;
    if (!ret) {
      return nullptr;
    }
    mStats.allocated_large += aSize;
    mStats.operations++;
  }
  if (num_dirty_after < num_dirty_before) {
    NotifySignificantReuse();
  }

  if (!aZero) {
    ApplyZeroOrJunk(ret, aSize);
  }

  return ret;
}

void* arena_t::Malloc(size_t aSize, bool aZero) {
  MOZ_DIAGNOSTIC_ASSERT(mMagic == ARENA_MAGIC);
  MOZ_ASSERT(aSize != 0);

  if (aSize <= gMaxBinClass) {
    return MallocSmall(aSize, aZero);
  }
  if (aSize <= gMaxLargeClass) {
    return MallocLarge(aSize, aZero);
  }
  return MallocHuge(aSize, aZero);
}

// Only handles large allocations that require more than page alignment.
void* arena_t::PallocLarge(size_t aAlignment, size_t aSize, size_t aAllocSize) {
  void* ret;
  size_t offset;
  arena_chunk_t* chunk;

  MOZ_ASSERT((aSize & gPageSizeMask) == 0);
  MOZ_ASSERT((aAlignment & gPageSizeMask) == 0);

  size_t num_dirty_before, num_dirty_after;
  {
    MaybeMutexAutoLock lock(mLock);
    num_dirty_before = mNumDirty;
    ret = AllocRun(aAllocSize, true, false);
    if (!ret) {
      return nullptr;
    }

    chunk = GetChunkForPtr(ret);

    offset = uintptr_t(ret) & (aAlignment - 1);
    MOZ_ASSERT((offset & gPageSizeMask) == 0);
    MOZ_ASSERT(offset < aAllocSize);
    if (offset == 0) {
      TrimRunTail(chunk, (arena_run_t*)ret, aAllocSize, aSize, false);
    } else {
      size_t leadsize, trailsize;

      leadsize = aAlignment - offset;
      if (leadsize > 0) {
        TrimRunHead(chunk, (arena_run_t*)ret, aAllocSize,
                    aAllocSize - leadsize);
        ret = (void*)(uintptr_t(ret) + leadsize);
      }

      trailsize = aAllocSize - leadsize - aSize;
      if (trailsize != 0) {
        // Trim trailing space.
        MOZ_ASSERT(trailsize < aAllocSize);
        TrimRunTail(chunk, (arena_run_t*)ret, aSize + trailsize, aSize, false);
      }
    }
    num_dirty_after = mNumDirty;

    mStats.allocated_large += aSize;
    mStats.operations++;
  }
  if (num_dirty_after < num_dirty_before) {
    NotifySignificantReuse();
  }
  // Note that since Bug 1488780we don't attempt purge dirty memory on this code
  // path. In general there won't be dirty memory above the threshold after an
  // allocation, only after free.  The exception is if the dirty page threshold
  // has changed.

  ApplyZeroOrJunk(ret, aSize);
  return ret;
}

void* arena_t::Palloc(size_t aAlignment, size_t aSize) {
  void* ret;
  size_t ceil_size;

  // Round size up to the nearest multiple of alignment.
  //
  // This done, we can take advantage of the fact that for each small
  // size class, every object is aligned at the smallest power of two
  // that is non-zero in the base two representation of the size.  For
  // example:
  //
  //   Size |   Base 2 | Minimum alignment
  //   -----+----------+------------------
  //     96 |  1100000 |  32
  //    144 | 10100000 |  32
  //    192 | 11000000 |  64
  //
  // Depending on runtime settings, it is possible that arena_malloc()
  // will further round up to a power of two, but that never causes
  // correctness issues.
  ceil_size = ALIGNMENT_CEILING(aSize, aAlignment);

  // (ceil_size < aSize) protects against the combination of maximal
  // alignment and size greater than maximal alignment.
  if (ceil_size < aSize) {
    // size_t overflow.
    return nullptr;
  }

  if (ceil_size <= gPageSize ||
      (aAlignment <= gPageSize && ceil_size <= gMaxLargeClass)) {
    ret = Malloc(ceil_size, false);
  } else {
    size_t run_size;

    // We can't achieve sub-page alignment, so round up alignment
    // permanently; it makes later calculations simpler.
    aAlignment = PAGE_CEILING(aAlignment);
    ceil_size = PAGE_CEILING(aSize);

    // (ceil_size < aSize) protects against very large sizes within
    // pagesize of SIZE_T_MAX.
    //
    // (ceil_size + aAlignment < ceil_size) protects against the
    // combination of maximal alignment and ceil_size large enough
    // to cause overflow.  This is similar to the first overflow
    // check above, but it needs to be repeated due to the new
    // ceil_size value, which may now be *equal* to maximal
    // alignment, whereas before we only detected overflow if the
    // original size was *greater* than maximal alignment.
    if (ceil_size < aSize || ceil_size + aAlignment < ceil_size) {
      // size_t overflow.
      return nullptr;
    }

    // Calculate the size of the over-size run that arena_palloc()
    // would need to allocate in order to guarantee the alignment.
    if (ceil_size >= aAlignment) {
      run_size = ceil_size + aAlignment - gPageSize;
    } else {
      // It is possible that (aAlignment << 1) will cause
      // overflow, but it doesn't matter because we also
      // subtract pagesize, which in the case of overflow
      // leaves us with a very large run_size.  That causes
      // the first conditional below to fail, which means
      // that the bogus run_size value never gets used for
      // anything important.
      run_size = (aAlignment << 1) - gPageSize;
    }

    if (run_size <= gMaxLargeClass) {
      ret = PallocLarge(aAlignment, ceil_size, run_size);
    } else if (aAlignment <= kChunkSize) {
      ret = MallocHuge(ceil_size, false);
    } else {
      ret = PallocHuge(ceil_size, aAlignment, false);
    }
  }

  MOZ_ASSERT((uintptr_t(ret) & (aAlignment - 1)) == 0);
  return ret;
}

class AllocInfo {
 public:
  template <bool Validate = false>
  static inline AllocInfo Get(const void* aPtr) {
    // If the allocator is not initialized, the pointer can't belong to it.
    if (Validate && !malloc_initialized) {
      return AllocInfo();
    }

    auto chunk = GetChunkForPtr(aPtr);
    if (Validate) {
      if (!chunk || !gChunkRTree.Get(chunk)) {
        return AllocInfo();
      }
    }

    if (chunk != aPtr) {
      MOZ_DIAGNOSTIC_ASSERT(chunk->mArena->mMagic == ARENA_MAGIC);
      size_t pageind = (((uintptr_t)aPtr - (uintptr_t)chunk) >> gPageSize2Pow);
      return GetInChunk(aPtr, chunk, pageind);
    }

    extent_node_t key;

    // Huge allocation
    key.mAddr = chunk;
    MutexAutoLock lock(huge_mtx);
    extent_node_t* node = huge.Search(&key);
    if (Validate && !node) {
      return AllocInfo();
    }
    return AllocInfo(node->mSize, node);
  }

  // Get the allocation information for a pointer we know is within a chunk
  // (Small or large, not huge).
  static inline AllocInfo GetInChunk(const void* aPtr, arena_chunk_t* aChunk,
                                     size_t pageind) {
    size_t mapbits = aChunk->mPageMap[pageind].bits;
    MOZ_DIAGNOSTIC_ASSERT((mapbits & CHUNK_MAP_ALLOCATED) != 0);

    size_t size;
    if ((mapbits & CHUNK_MAP_LARGE) == 0) {
      arena_run_t* run = (arena_run_t*)(mapbits & ~gPageSizeMask);
      MOZ_DIAGNOSTIC_ASSERT(run->mMagic == ARENA_RUN_MAGIC);
      size = run->mBin->mSizeClass;
    } else {
      size = mapbits & ~gPageSizeMask;
      MOZ_DIAGNOSTIC_ASSERT(size != 0);
    }

    return AllocInfo(size, aChunk);
  }

  // Validate ptr before assuming that it points to an allocation.  Currently,
  // the following validation is performed:
  //
  // + Check that ptr is not nullptr.
  //
  // + Check that ptr lies within a mapped chunk.
  static inline AllocInfo GetValidated(const void* aPtr) {
    return Get<true>(aPtr);
  }

  AllocInfo() : mSize(0), mChunk(nullptr) {}

  explicit AllocInfo(size_t aSize, arena_chunk_t* aChunk)
      : mSize(aSize), mChunk(aChunk) {
    MOZ_ASSERT(mSize <= gMaxLargeClass);
  }

  explicit AllocInfo(size_t aSize, extent_node_t* aNode)
      : mSize(aSize), mNode(aNode) {
    MOZ_ASSERT(mSize > gMaxLargeClass);
  }

  size_t Size() { return mSize; }

  arena_t* Arena() {
    if (mSize <= gMaxLargeClass) {
      return mChunk->mArena;
    }
    // Best effort detection that we're not trying to access an already
    // disposed arena. In the case of a disposed arena, the memory location
    // pointed by mNode->mArena is either free (but still a valid memory
    // region, per TypedBaseAlloc<arena_t>), in which case its id was reset,
    // or has been reallocated for a new region, and its id is very likely
    // different (per randomness). In both cases, the id is unlikely to
    // match what it was for the disposed arena.
    MOZ_RELEASE_ASSERT(mNode->mArenaId == mNode->mArena->mId);
    return mNode->mArena;
  }

  bool IsValid() const { return !!mSize; }

 private:
  size_t mSize;
  union {
    // Pointer to the chunk associated with the allocation for small
    // and large allocations.
    arena_chunk_t* mChunk;

    // Pointer to the extent node for huge allocations.
    extent_node_t* mNode;
  };
};

inline void MozJemalloc::jemalloc_ptr_info(const void* aPtr,
                                           jemalloc_ptr_info_t* aInfo) {
  arena_chunk_t* chunk = GetChunkForPtr(aPtr);

  // Is the pointer null, or within one chunk's size of null?
  // Alternatively, if the allocator is not initialized yet, the pointer
  // can't be known.
  if (!chunk || !malloc_initialized) {
    *aInfo = {TagUnknown, nullptr, 0, 0};
    return;
  }

  // Look for huge allocations before looking for |chunk| in gChunkRTree.
  // This is necessary because |chunk| won't be in gChunkRTree if it's
  // the second or subsequent chunk in a huge allocation.
  extent_node_t* node;
  extent_node_t key;
  {
    MutexAutoLock lock(huge_mtx);
    key.mAddr = const_cast<void*>(aPtr);
    node =
        reinterpret_cast<RedBlackTree<extent_node_t, ExtentTreeBoundsTrait>*>(
            &huge)
            ->Search(&key);
    if (node) {
      *aInfo = {TagLiveAlloc, node->mAddr, node->mSize, node->mArena->mId};
      return;
    }
  }

  // It's not a huge allocation. Check if we have a known chunk.
  if (!gChunkRTree.Get(chunk)) {
    *aInfo = {TagUnknown, nullptr, 0, 0};
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(chunk->mArena->mMagic == ARENA_MAGIC);

  // Get the page number within the chunk.
  size_t pageind = (((uintptr_t)aPtr - (uintptr_t)chunk) >> gPageSize2Pow);
  if (pageind < gChunkHeaderNumPages) {
    // Within the chunk header.
    *aInfo = {TagUnknown, nullptr, 0, 0};
    return;
  }

  size_t mapbits = chunk->mPageMap[pageind].bits;

  if (!(mapbits & CHUNK_MAP_ALLOCATED)) {
    void* pageaddr = (void*)(uintptr_t(aPtr) & ~gPageSizeMask);
    *aInfo = {TagFreedPage, pageaddr, gPageSize, chunk->mArena->mId};
    return;
  }

  if (mapbits & CHUNK_MAP_LARGE) {
    // It's a large allocation. Only the first page of a large
    // allocation contains its size, so if the address is not in
    // the first page, scan back to find the allocation size.
    size_t size;
    while (true) {
      size = mapbits & ~gPageSizeMask;
      if (size != 0) {
        break;
      }

      // The following two return paths shouldn't occur in
      // practice unless there is heap corruption.
      pageind--;
      MOZ_DIAGNOSTIC_ASSERT(pageind >= gChunkHeaderNumPages);
      if (pageind < gChunkHeaderNumPages) {
        *aInfo = {TagUnknown, nullptr, 0, 0};
        return;
      }

      mapbits = chunk->mPageMap[pageind].bits;
      MOZ_DIAGNOSTIC_ASSERT(mapbits & CHUNK_MAP_LARGE);
      if (!(mapbits & CHUNK_MAP_LARGE)) {
        *aInfo = {TagUnknown, nullptr, 0, 0};
        return;
      }
    }

    void* addr = ((char*)chunk) + (pageind << gPageSize2Pow);
    *aInfo = {TagLiveAlloc, addr, size, chunk->mArena->mId};
    return;
  }

  // It must be a small allocation.
  auto run = (arena_run_t*)(mapbits & ~gPageSizeMask);
  MOZ_DIAGNOSTIC_ASSERT(run->mMagic == ARENA_RUN_MAGIC);

  // The allocation size is stored in the run metadata.
  size_t size = run->mBin->mSizeClass;

  // Address of the first possible pointer in the run after its headers.
  uintptr_t reg0_addr = (uintptr_t)run + run->mBin->mRunFirstRegionOffset;
  if (aPtr < (void*)reg0_addr) {
    // In the run header.
    *aInfo = {TagUnknown, nullptr, 0, 0};
    return;
  }

  // Position in the run.
  unsigned regind = ((uintptr_t)aPtr - reg0_addr) / size;

  // Pointer to the allocation's base address.
  void* addr = (void*)(reg0_addr + regind * size);

  // Check if the allocation has been freed.
  unsigned elm = regind >> (LOG2(sizeof(int)) + 3);
  unsigned bit = regind - (elm << (LOG2(sizeof(int)) + 3));
  PtrInfoTag tag =
      ((run->mRegionsMask[elm] & (1U << bit))) ? TagFreedAlloc : TagLiveAlloc;

  *aInfo = {tag, addr, size, chunk->mArena->mId};
}

namespace Debug {
// Helper for debuggers. We don't want it to be inlined and optimized out.
MOZ_NEVER_INLINE jemalloc_ptr_info_t* jemalloc_ptr_info(const void* aPtr) {
  static jemalloc_ptr_info_t info;
  MozJemalloc::jemalloc_ptr_info(aPtr, &info);
  return &info;
}
}  // namespace Debug

arena_chunk_t* arena_t::DallocSmall(arena_chunk_t* aChunk, void* aPtr,
                                    arena_chunk_map_t* aMapElm) {
  arena_run_t* run;
  arena_bin_t* bin;
  size_t size;

  run = (arena_run_t*)(aMapElm->bits & ~gPageSizeMask);
  MOZ_DIAGNOSTIC_ASSERT(run->mMagic == ARENA_RUN_MAGIC);
  bin = run->mBin;
  size = bin->mSizeClass;
  MOZ_DIAGNOSTIC_ASSERT(uintptr_t(aPtr) >=
                        uintptr_t(run) + bin->mRunFirstRegionOffset);

  arena_run_reg_dalloc(run, bin, aPtr, size);
  run->mNumFree++;
  arena_chunk_t* dealloc_chunk = nullptr;

  if (run->mNumFree == bin->mRunNumRegions) {
    // This run is entirely freed, remove it from our bin.
#if defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
    run->mMagic = 0;
#endif
    MOZ_ASSERT(bin->mNonFullRuns.ElementProbablyInList(run));
    bin->mNonFullRuns.remove(run);
    dealloc_chunk = DallocRun(run, true);
    bin->mNumRuns--;
  } else if (run->mNumFree == 1) {
    // This is first slot we freed from this run, start tracking.
    MOZ_ASSERT(!bin->mNonFullRuns.ElementProbablyInList(run));
    bin->mNonFullRuns.pushFront(run);
  }
  // else we just keep the run in mNonFullRuns where it is.
  // Note that we could move it to the head of the list here to get a strict
  // "most-recently-freed" order, but some of our benchmarks seem to be more
  // sensible to the increased overhead that this brings than to the order
  // supposedly slightly better for keeping CPU caches warm if we do.
  // In general we cannot foresee the future, so any order we choose might
  // perform different for different use cases and needs to be balanced with
  // the book-keeping overhead via measurements.

  mStats.allocated_small -= size;
  mStats.operations++;

  return dealloc_chunk;
}

arena_chunk_t* arena_t::DallocLarge(arena_chunk_t* aChunk, void* aPtr) {
  MOZ_DIAGNOSTIC_ASSERT((uintptr_t(aPtr) & gPageSizeMask) == 0);
  size_t pageind = (uintptr_t(aPtr) - uintptr_t(aChunk)) >> gPageSize2Pow;
  size_t size = aChunk->mPageMap[pageind].bits & ~gPageSizeMask;

  mStats.allocated_large -= size;
  mStats.operations++;

  return DallocRun((arena_run_t*)aPtr, true);
}

static inline void arena_dalloc(void* aPtr, size_t aOffset, arena_t* aArena) {
  MOZ_ASSERT(aPtr);
  MOZ_ASSERT(aOffset != 0);
  MOZ_ASSERT(GetChunkOffsetForPtr(aPtr) == aOffset);

  auto chunk = (arena_chunk_t*)((uintptr_t)aPtr - aOffset);
  auto arena = chunk->mArena;
  MOZ_ASSERT(arena);
  MOZ_DIAGNOSTIC_ASSERT(arena->mMagic == ARENA_MAGIC);
  MOZ_RELEASE_ASSERT(!aArena || arena == aArena);

  size_t pageind = aOffset >> gPageSize2Pow;
  if (opt_poison) {
    AllocInfo info = AllocInfo::GetInChunk(aPtr, chunk, pageind);
    MOZ_ASSERT(info.IsValid());
    MaybePoison(aPtr, info.Size());
  }

  arena_chunk_t* chunk_dealloc_delay = nullptr;
  purge_action_t purge_action;
  {
    MOZ_DIAGNOSTIC_ASSERT(arena->mLock.SafeOnThisThread());
    MaybeMutexAutoLock lock(arena->mLock);
    arena_chunk_map_t* mapelm = &chunk->mPageMap[pageind];
    MOZ_RELEASE_ASSERT(
        (mapelm->bits &
         (CHUNK_MAP_FRESH_MADVISED_OR_DECOMMITTED | CHUNK_MAP_ZEROED)) == 0,
        "Freeing in a page with bad bits.");
    MOZ_RELEASE_ASSERT((mapelm->bits & CHUNK_MAP_ALLOCATED) != 0,
                       "Double-free?");
    if ((mapelm->bits & CHUNK_MAP_LARGE) == 0) {
      // Small allocation.
      chunk_dealloc_delay = arena->DallocSmall(chunk, aPtr, mapelm);
    } else {
      // Large allocation.
      chunk_dealloc_delay = arena->DallocLarge(chunk, aPtr);
    }

    purge_action = arena->ShouldStartPurge();
  }

  if (chunk_dealloc_delay) {
    chunk_dealloc((void*)chunk_dealloc_delay, kChunkSize, ARENA_CHUNK);
  }

  arena->MayDoOrQueuePurge(purge_action, "arena_dalloc");
}

static inline void idalloc(void* ptr, arena_t* aArena) {
  size_t offset;

  MOZ_ASSERT(ptr);

  offset = GetChunkOffsetForPtr(ptr);
  if (offset != 0) {
    arena_dalloc(ptr, offset, aArena);
  } else {
    huge_dalloc(ptr, aArena);
  }
}

inline purge_action_t arena_t::ShouldStartPurge() {
  if (mNumDirty > mMaxDirty) {
    if (!mIsDeferredPurgeEnabled) {
      return purge_action_t::PurgeNow;
    }
    if (mIsPurgePending) {
      return purge_action_t::None;
    }
    mIsPurgePending = true;
    return purge_action_t::Queue;
  }
  return purge_action_t::None;
}

inline void arena_t::MayDoOrQueuePurge(purge_action_t aAction,
                                       const char* aCaller) {
  switch (aAction) {
    case purge_action_t::Queue:
      // Note that this thread committed earlier by setting
      // mIsDeferredPurgePending to add us to the list. There is a low
      // chance that in the meantime another thread ran Purge() and cleared
      // the flag, but that is fine, we'll adjust our bookkeeping when calling
      // ShouldStartPurge() or Purge() next time.
      gArenas.AddToOutstandingPurges(this);
      break;
    case purge_action_t::PurgeNow: {
      ArenaPurgeResult pr = PurgeLoop(PurgeIfThreshold, aCaller);
      // Arenas cannot die here because the caller is still using the arena, if
      // they did it'd be a use-after-free: the arena is destroyed but then used
      // afterwards.
      MOZ_RELEASE_ASSERT(pr != ArenaPurgeResult::Dying);
      break;
    }
    case purge_action_t::None:
      // do nothing.
      break;
  }
}

inline void arena_t::NotifySignificantReuse() {
  // Note that there is a chance here for a race between threads calling
  // GetTimeStampNS in a different order than writing it to the Atomic,
  // resulting in mLastSignificantReuseNS going potentially backwards.
  // Our use case is not sensitive to small deviations, the worse that can
  // happen is a slightly earlier purge.
  mLastSignificantReuseNS = GetTimestampNS();
}

void arena_t::RallocShrinkLarge(arena_chunk_t* aChunk, void* aPtr, size_t aSize,
                                size_t aOldSize) {
  MOZ_ASSERT(aSize < aOldSize);

  // Shrink the run, and make trailing pages available for other
  // allocations.
  purge_action_t purge_action;
  {
    MaybeMutexAutoLock lock(mLock);
    TrimRunTail(aChunk, (arena_run_t*)aPtr, aOldSize, aSize, true);
    mStats.allocated_large -= aOldSize - aSize;
    mStats.operations++;

    purge_action = ShouldStartPurge();
  }
  MayDoOrQueuePurge(purge_action, "RallocShrinkLarge");
}

// Returns whether reallocation was successful.
bool arena_t::RallocGrowLarge(arena_chunk_t* aChunk, void* aPtr, size_t aSize,
                              size_t aOldSize) {
  size_t pageind = (uintptr_t(aPtr) - uintptr_t(aChunk)) >> gPageSize2Pow;
  size_t npages = aOldSize >> gPageSize2Pow;

  size_t num_dirty_before, num_dirty_after;
  {
    MaybeMutexAutoLock lock(mLock);
    MOZ_DIAGNOSTIC_ASSERT(aOldSize ==
                          (aChunk->mPageMap[pageind].bits & ~gPageSizeMask));

    // Try to extend the run.
    MOZ_ASSERT(aSize > aOldSize);
    if (pageind + npages < gChunkNumPages - 1 &&
        (aChunk->mPageMap[pageind + npages].bits &
         (CHUNK_MAP_ALLOCATED | CHUNK_MAP_BUSY)) == 0 &&
        (aChunk->mPageMap[pageind + npages].bits & ~gPageSizeMask) >=
            aSize - aOldSize) {
      num_dirty_before = mNumDirty;
      // The next run is available and sufficiently large.  Split the
      // following run, then merge the first part with the existing
      // allocation.
      if (!SplitRun((arena_run_t*)(uintptr_t(aChunk) +
                                   ((pageind + npages) << gPageSize2Pow)),
                    aSize - aOldSize, true, false)) {
        return false;
      }

      aChunk->mPageMap[pageind].bits =
          aSize | CHUNK_MAP_LARGE | CHUNK_MAP_ALLOCATED;
      aChunk->mPageMap[pageind + npages].bits =
          CHUNK_MAP_LARGE | CHUNK_MAP_ALLOCATED;

      mStats.allocated_large += aSize - aOldSize;
      mStats.operations++;
      num_dirty_after = mNumDirty;
    } else {
      return false;
    }
  }
  if (num_dirty_after < num_dirty_before) {
    NotifySignificantReuse();
  }
  return true;
}

#ifdef XP_DARWIN
#  define VM_COPY_MIN kChunkSize
static inline void pages_copy(void* dest, const void* src, size_t n) {
  MOZ_ASSERT((void*)((uintptr_t)dest & ~gPageSizeMask) == dest);
  MOZ_ASSERT(n >= VM_COPY_MIN);
  MOZ_ASSERT((void*)((uintptr_t)src & ~gPageSizeMask) == src);

  kern_return_t r = vm_copy(mach_task_self(), (vm_address_t)src, (vm_size_t)n,
                            (vm_address_t)dest);
  if (r != KERN_SUCCESS) {
    MOZ_CRASH("vm_copy() failed");
  }
}
#endif

void* arena_t::RallocSmallOrLarge(void* aPtr, size_t aSize, size_t aOldSize) {
  void* ret;
  size_t copysize;
  SizeClass sizeClass(aSize);

  // Try to avoid moving the allocation.
  if (aOldSize <= gMaxLargeClass && sizeClass.Size() == aOldSize) {
    if (aSize < aOldSize) {
      MaybePoison((void*)(uintptr_t(aPtr) + aSize), aOldSize - aSize);
    }
    return aPtr;
  }
  if (sizeClass.Type() == SizeClass::Large && aOldSize > gMaxBinClass &&
      aOldSize <= gMaxLargeClass) {
    arena_chunk_t* chunk = GetChunkForPtr(aPtr);
    if (sizeClass.Size() < aOldSize) {
      // Fill before shrinking in order to avoid a race.
      MaybePoison((void*)((uintptr_t)aPtr + aSize), aOldSize - aSize);
      RallocShrinkLarge(chunk, aPtr, sizeClass.Size(), aOldSize);
      return aPtr;
    }
    if (RallocGrowLarge(chunk, aPtr, sizeClass.Size(), aOldSize)) {
      ApplyZeroOrJunk((void*)((uintptr_t)aPtr + aOldSize), aSize - aOldSize);
      return aPtr;
    }
  }

  // If we get here, then aSize and aOldSize are different enough that we
  // need to move the object or the run can't be expanded because the memory
  // after it is allocated or busy.  In that case, fall back to allocating new
  // space and copying. Allow non-private arenas to switch arenas.
  ret = (mIsPrivate ? this : choose_arena(aSize))->Malloc(aSize, false);
  if (!ret) {
    return nullptr;
  }

  // Junk/zero-filling were already done by arena_t::Malloc().
  copysize = (aSize < aOldSize) ? aSize : aOldSize;
#ifdef VM_COPY_MIN
  if (copysize >= VM_COPY_MIN) {
    pages_copy(ret, aPtr, copysize);
  } else
#endif
  {
    memcpy(ret, aPtr, copysize);
  }
  idalloc(aPtr, this);
  return ret;
}

void* arena_t::Ralloc(void* aPtr, size_t aSize, size_t aOldSize) {
  MOZ_DIAGNOSTIC_ASSERT(mMagic == ARENA_MAGIC);
  MOZ_ASSERT(aPtr);
  MOZ_ASSERT(aSize != 0);

  return (aSize <= gMaxLargeClass) ? RallocSmallOrLarge(aPtr, aSize, aOldSize)
                                   : RallocHuge(aPtr, aSize, aOldSize);
}

void* arena_t::operator new(size_t aCount, const fallible_t&) noexcept {
  MOZ_ASSERT(aCount == sizeof(arena_t));
  return TypedBaseAlloc<arena_t>::alloc();
}

void arena_t::operator delete(void* aPtr) {
  TypedBaseAlloc<arena_t>::dealloc((arena_t*)aPtr);
}

arena_t::arena_t(arena_params_t* aParams, bool aIsPrivate) {
  unsigned i;

  memset(&mLink, 0, sizeof(mLink));
  memset(&mStats, 0, sizeof(arena_stats_t));
  mId = 0;

  // Initialize chunks.
  mChunksDirty.Init();
#ifdef MALLOC_DOUBLE_PURGE
  new (&mChunksMAdvised) DoublyLinkedList<arena_chunk_t>();
#endif
  mSpare = nullptr;

  mRandomizeSmallAllocations = opt_randomize_small;
  MaybeMutex::DoLock doLock = MaybeMutex::MUST_LOCK;
  if (aParams) {
    uint32_t randFlags = aParams->mFlags & ARENA_FLAG_RANDOMIZE_SMALL_MASK;
    switch (randFlags) {
      case ARENA_FLAG_RANDOMIZE_SMALL_ENABLED:
        mRandomizeSmallAllocations = true;
        break;
      case ARENA_FLAG_RANDOMIZE_SMALL_DISABLED:
        mRandomizeSmallAllocations = false;
        break;
      case ARENA_FLAG_RANDOMIZE_SMALL_DEFAULT:
      default:
        break;
    }

    uint32_t threadFlags = aParams->mFlags & ARENA_FLAG_THREAD_MASK;
    if (threadFlags == ARENA_FLAG_THREAD_MAIN_THREAD_ONLY) {
      // At the moment we require that any ARENA_FLAG_THREAD_MAIN_THREAD_ONLY
      // arenas are created and therefore always accessed by the main thread.
      // This is for two reasons:
      //  * it allows jemalloc_stats to read their statistics (we also require
      //    that jemalloc_stats is only used on the main thread).
      //  * Only main-thread or threadsafe arenas can be guanteed to be in a
      //    consistent state after a fork() from the main thread.  If fork()
      //    occurs off-thread then the new child process cannot use these arenas
      //    (new children should usually exec() or exit() since other data may
      //    also be inconsistent).
      MOZ_ASSERT(gArenas.IsOnMainThread());
      MOZ_ASSERT(aIsPrivate);
      doLock = MaybeMutex::AVOID_LOCK_UNSAFE;
    }

    mMaxDirtyIncreaseOverride = aParams->mMaxDirtyIncreaseOverride;
    mMaxDirtyDecreaseOverride = aParams->mMaxDirtyDecreaseOverride;

    if (aParams->mLabel) {
      // The string may be truncated so always place a null-byte in the last
      // position.
      strncpy(mLabel, aParams->mLabel, LABEL_MAX_CAPACITY - 1);
      mLabel[LABEL_MAX_CAPACITY - 1] = 0;

      // If the string was trucated, then replace it's end with "..."
      if (strlen(aParams->mLabel) >= LABEL_MAX_CAPACITY) {
        for (int i = 0; i < 3; i++) {
          mLabel[LABEL_MAX_CAPACITY - 2 - i] = '.';
        }
      }
    } else {
      mLabel[0] = 0;
    }
  } else {
    mMaxDirtyIncreaseOverride = 0;
    mMaxDirtyDecreaseOverride = 0;

    mLabel[0] = 0;
  }

  mLastSignificantReuseNS = GetTimestampNS();
  mIsPurgePending = false;
  mIsDeferredPurgeEnabled = gArenas.IsDeferredPurgeEnabled();

  MOZ_RELEASE_ASSERT(mLock.Init(doLock));

  mPRNG = nullptr;
  mIsPRNGInitializing = false;

  mIsPrivate = aIsPrivate;

  mNumDirty = 0;
  mNumFresh = 0;
  mNumMAdvised = 0;
  // The default maximum amount of dirty pages allowed on arenas is a fraction
  // of opt_dirty_max.
  mMaxDirtyBase = (aParams && aParams->mMaxDirty) ? aParams->mMaxDirty
                                                  : (opt_dirty_max / 8);
  UpdateMaxDirty();

  mRunsAvail.Init();

  // Initialize bins.
  SizeClass sizeClass(1);

  for (i = 0;; i++) {
    arena_bin_t& bin = mBins[i];
    bin.Init(sizeClass);

    // SizeClass doesn't want sizes larger than gMaxBinClass for now.
    if (sizeClass.Size() == gMaxBinClass) {
      break;
    }
    sizeClass = sizeClass.Next();
  }
  MOZ_ASSERT(i == NUM_SMALL_CLASSES - 1);

#if defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
  mMagic = ARENA_MAGIC;
#endif
}

arena_t::~arena_t() {
  size_t i;
  MaybeMutexAutoLock lock(mLock);

  MOZ_RELEASE_ASSERT(!mLink.Left() && !mLink.Right(),
                     "Arena is still registered");
  MOZ_RELEASE_ASSERT(!mStats.allocated_small && !mStats.allocated_large,
                     "Arena is not empty");
  if (mSpare) {
    chunk_dealloc(mSpare, kChunkSize, ARENA_CHUNK);
  }
  for (i = 0; i < NUM_SMALL_CLASSES; i++) {
    MOZ_RELEASE_ASSERT(mBins[i].mNonFullRuns.isEmpty(), "Bin is not empty");
  }
#ifdef MOZ_DEBUG
  {
    MutexAutoLock lock(huge_mtx);
    // This is an expensive check, so we only do it on debug builds.
    for (auto node : huge.iter()) {
      MOZ_RELEASE_ASSERT(node->mArenaId != mId, "Arena has huge allocations");
    }
  }
#endif
  mId = 0;
}

arena_t* ArenaCollection::CreateArena(bool aIsPrivate,
                                      arena_params_t* aParams) {
  arena_t* ret = new (fallible) arena_t(aParams, aIsPrivate);
  if (!ret) {
    // Only reached if there is an OOM error.

    // OOM here is quite inconvenient to propagate, since dealing with it
    // would require a check for failure in the fast path.  Instead, punt
    // by using the first arena.
    // In practice, this is an extremely unlikely failure.
    _malloc_message(_getprogname(), ": (malloc) Error initializing arena\n");

    return mDefaultArena;
  }

  MutexAutoLock lock(mLock);

  // For public arenas, it's fine to just use incrementing arena id
  if (!aIsPrivate) {
    ret->mId = mLastPublicArenaId++;
    mArenas.Insert(ret);
    return ret;
  }

#ifdef NON_RANDOM_ARENA_IDS
  // For private arenas, slightly obfuscate the id by XORing a key generated
  // once, and rotate the bits by an amount also generated once.
  if (mArenaIdKey == 0) {
    mozilla::Maybe<uint64_t> maybeRandom = mozilla::RandomUint64();
    MOZ_RELEASE_ASSERT(maybeRandom.isSome());
    mArenaIdKey = maybeRandom.value();
    maybeRandom = mozilla::RandomUint64();
    MOZ_RELEASE_ASSERT(maybeRandom.isSome());
    mArenaIdRotation = maybeRandom.value() & (sizeof(void*) * 8 - 1);
  }
  arena_id_t id = reinterpret_cast<arena_id_t>(ret) ^ mArenaIdKey;
  ret->mId =
      (id >> mArenaIdRotation) | (id << (sizeof(void*) * 8 - mArenaIdRotation));
  mPrivateArenas.Insert(ret);
  return ret;
#else
  // For private arenas, generate a cryptographically-secure random id for the
  // new arena. If an attacker manages to get control of the process, this
  // should make it more difficult for them to "guess" the ID of a memory
  // arena, stopping them from getting data they may want
  Tree& tree = (ret->IsMainThreadOnly()) ? mMainThreadArenas : mPrivateArenas;
  arena_id_t arena_id;
  do {
    arena_id = MakeRandArenaId(ret->IsMainThreadOnly());
    // Keep looping until we ensure that the random number we just generated
    // isn't already in use by another active arena
  } while (GetByIdInternal(tree, arena_id));

  ret->mId = arena_id;
  tree.Insert(ret);
  return ret;
#endif
}

#ifndef NON_RANDOM_ARENA_IDS
arena_id_t ArenaCollection::MakeRandArenaId(bool aIsMainThreadOnly) const {
  uint64_t rand;
  do {
    mozilla::Maybe<uint64_t> maybeRandomId = mozilla::RandomUint64();
    MOZ_RELEASE_ASSERT(maybeRandomId.isSome());

    rand = maybeRandomId.value();

    // Set or clear the least significant bit depending on if this is a
    // main-thread-only arena.  We use this in GetById.
    if (aIsMainThreadOnly) {
      rand = rand | MAIN_THREAD_ARENA_BIT;
    } else {
      rand = rand & ~MAIN_THREAD_ARENA_BIT;
    }

    // Avoid 0 as an arena Id. We use 0 for disposed arenas.
  } while (rand == 0);

  return arena_id_t(rand);
}
#endif

// End arena.
// ***************************************************************************
// Begin general internal functions.

// Initialize huge allocation data.
static void huge_init() MOZ_REQUIRES(gInitLock) {
  huge_mtx.Init();
  MOZ_PUSH_IGNORE_THREAD_SAFETY
  huge.Init();
  huge_allocated = 0;
  huge_mapped = 0;
  huge_operations = 0;
  MOZ_POP_THREAD_SAFETY
}

void* arena_t::MallocHuge(size_t aSize, bool aZero) {
  return PallocHuge(aSize, kChunkSize, aZero);
}

void* arena_t::PallocHuge(size_t aSize, size_t aAlignment, bool aZero) {
  void* ret;
  size_t csize;
  size_t psize;
  extent_node_t* node;

  // We're going to configure guard pages in the region between the
  // page-aligned size and the chunk-aligned size, so if those are the same
  // then we need to force that region into existence.
  csize = CHUNK_CEILING(aSize + gPageSize);
  if (csize < aSize) {
    // size is large enough to cause size_t wrap-around.
    return nullptr;
  }

  // Allocate an extent node with which to track the chunk.
  node = ExtentAlloc::alloc();
  if (!node) {
    return nullptr;
  }

  // Allocate one or more contiguous chunks for this request.
  ret = chunk_alloc(csize, aAlignment, false);
  if (!ret) {
    ExtentAlloc::dealloc(node);
    return nullptr;
  }
  psize = PAGE_CEILING(aSize);
#ifdef MOZ_DEBUG
  if (aZero) {
    chunk_assert_zero(ret, psize);
  }
#endif

  // Insert node into huge.
  node->mAddr = ret;
  node->mSize = psize;
  node->mArena = this;
  node->mArenaId = mId;

  {
    MutexAutoLock lock(huge_mtx);
    huge.Insert(node);

    // Although we allocated space for csize bytes, we indicate that we've
    // allocated only psize bytes.
    //
    // If DECOMMIT is defined, this is a reasonable thing to do, since
    // we'll explicitly decommit the bytes in excess of psize.
    //
    // If DECOMMIT is not defined, then we're relying on the OS to be lazy
    // about how it allocates physical pages to mappings.  If we never
    // touch the pages in excess of psize, the OS won't allocate a physical
    // page, and we won't use more than psize bytes of physical memory.
    //
    // A correct program will only touch memory in excess of how much it
    // requested if it first calls malloc_usable_size and finds out how
    // much space it has to play with.  But because we set node->mSize =
    // psize above, malloc_usable_size will return psize, not csize, and
    // the program will (hopefully) never touch bytes in excess of psize.
    // Thus those bytes won't take up space in physical memory, and we can
    // reasonably claim we never "allocated" them in the first place.
    huge_allocated += psize;
    huge_mapped += csize;
    huge_operations++;
  }

  pages_decommit((void*)((uintptr_t)ret + psize), csize - psize);

  if (!aZero) {
    ApplyZeroOrJunk(ret, psize);
  }

  return ret;
}

void* arena_t::RallocHuge(void* aPtr, size_t aSize, size_t aOldSize) {
  void* ret;
  size_t copysize;

  // Avoid moving the allocation if the size class would not change.
  if (aOldSize > gMaxLargeClass &&
      CHUNK_CEILING(aSize + gPageSize) == CHUNK_CEILING(aOldSize + gPageSize)) {
    size_t psize = PAGE_CEILING(aSize);
    if (aSize < aOldSize) {
      MaybePoison((void*)((uintptr_t)aPtr + aSize), aOldSize - aSize);
    }
    if (psize < aOldSize) {
      extent_node_t key;

      pages_decommit((void*)((uintptr_t)aPtr + psize), aOldSize - psize);

      // Update recorded size.
      MutexAutoLock lock(huge_mtx);
      key.mAddr = const_cast<void*>(aPtr);
      extent_node_t* node = huge.Search(&key);
      MOZ_ASSERT(node);
      MOZ_ASSERT(node->mSize == aOldSize);
      MOZ_RELEASE_ASSERT(node->mArena == this);
      huge_allocated -= aOldSize - psize;
      huge_operations++;
      // No need to change huge_mapped, because we didn't (un)map anything.
      node->mSize = psize;
    } else if (psize > aOldSize) {
      if (!pages_commit((void*)((uintptr_t)aPtr + aOldSize),
                        psize - aOldSize)) {
        return nullptr;
      }

      // We need to update the recorded size if the size increased,
      // so malloc_usable_size doesn't return a value smaller than
      // what was requested via realloc().
      extent_node_t key;
      MutexAutoLock lock(huge_mtx);
      key.mAddr = const_cast<void*>(aPtr);
      extent_node_t* node = huge.Search(&key);
      MOZ_ASSERT(node);
      MOZ_ASSERT(node->mSize == aOldSize);
      MOZ_RELEASE_ASSERT(node->mArena == this);
      huge_allocated += psize - aOldSize;
      huge_operations++;
      // No need to change huge_mapped, because we didn't
      // (un)map anything.
      node->mSize = psize;
    }

    if (aSize > aOldSize) {
      ApplyZeroOrJunk((void*)((uintptr_t)aPtr + aOldSize), aSize - aOldSize);
    }
    return aPtr;
  }

  // If we get here, then aSize and aOldSize are different enough that we
  // need to use a different size class.  In that case, fall back to allocating
  // new space and copying. Allow non-private arenas to switch arenas.
  ret = (mIsPrivate ? this : choose_arena(aSize))->MallocHuge(aSize, false);
  if (!ret) {
    return nullptr;
  }

  copysize = (aSize < aOldSize) ? aSize : aOldSize;
#ifdef VM_COPY_MIN
  if (copysize >= VM_COPY_MIN) {
    pages_copy(ret, aPtr, copysize);
  } else
#endif
  {
    memcpy(ret, aPtr, copysize);
  }
  idalloc(aPtr, this);
  return ret;
}

static void huge_dalloc(void* aPtr, arena_t* aArena) {
  extent_node_t* node;
  size_t mapped = 0;
  {
    extent_node_t key;
    MutexAutoLock lock(huge_mtx);

    // Extract from tree of huge allocations.
    key.mAddr = aPtr;
    node = huge.Search(&key);
    MOZ_RELEASE_ASSERT(node, "Double-free?");
    MOZ_ASSERT(node->mAddr == aPtr);
    MOZ_RELEASE_ASSERT(!aArena || node->mArena == aArena);
    // See AllocInfo::Arena.
    MOZ_RELEASE_ASSERT(node->mArenaId == node->mArena->mId);
    huge.Remove(node);

    mapped = CHUNK_CEILING(node->mSize + gPageSize);
    huge_allocated -= node->mSize;
    huge_mapped -= mapped;
    huge_operations++;
  }

  // Unmap chunk.
  chunk_dealloc(node->mAddr, mapped, HUGE_CHUNK);

  ExtentAlloc::dealloc(node);
}

// Returns whether the allocator was successfully initialized.
static bool malloc_init_hard() {
  unsigned i;
  const char* opts;

  AutoLock<StaticMutex> lock(gInitLock);

  if (malloc_initialized) {
    // Another thread initialized the allocator before this one
    // acquired gInitLock.
    return true;
  }

  if (!thread_arena.init()) {
    return true;
  }

  // Get page size and number of CPUs
  const size_t page_size = GetKernelPageSize();
  // We assume that the page size is a power of 2.
  MOZ_ASSERT(IsPowerOfTwo(page_size));
#ifdef MALLOC_STATIC_PAGESIZE
  if (gPageSize % page_size) {
    _malloc_message(
        _getprogname(),
        "Compile-time page size does not divide the runtime one.\n");
    MOZ_CRASH();
  }
#else
  gRealPageSize = gPageSize = page_size;
#endif

  // Get runtime configuration.
  if ((opts = getenv("MALLOC_OPTIONS"))) {
    for (i = 0; opts[i] != '\0'; i++) {
      // All options are single letters, some take a *prefix* numeric argument.

      // Parse the argument.
      unsigned prefix_arg = 0;
      while (opts[i] >= '0' && opts[i] <= '9') {
        prefix_arg *= 10;
        prefix_arg += opts[i] - '0';
        i++;
      }

      switch (opts[i]) {
        case 'f':
          opt_dirty_max >>= prefix_arg ? prefix_arg : 1;
          break;
        case 'F':
          prefix_arg = prefix_arg ? prefix_arg : 1;
          if (opt_dirty_max == 0) {
            opt_dirty_max = 1;
            prefix_arg--;
          }
          opt_dirty_max <<= prefix_arg;
          if (opt_dirty_max == 0) {
            // If the shift above overflowed all the bits then clamp the result
            // instead.  If we started with DIRTY_MAX_DEFAULT then this will
            // always be a power of two so choose the maximum power of two that
            // fits in a size_t.
            opt_dirty_max = size_t(1) << (sizeof(size_t) * CHAR_BIT - 1);
          }
          break;
#ifdef MALLOC_RUNTIME_CONFIG
        case 'j':
          opt_junk = false;
          break;
        case 'J':
          opt_junk = true;
          break;
        case 'q':
          // The argument selects how much poisoning to do.
          opt_poison = NONE;
          break;
        case 'Q':
          if (opts[i + 1] == 'Q') {
            // Maximum poisoning.
            i++;
            opt_poison = ALL;
          } else {
            opt_poison = SOME;
            opt_poison_size = kCacheLineSize * prefix_arg;
          }
          break;
        case 'z':
          opt_zero = false;
          break;
        case 'Z':
          opt_zero = true;
          break;
#  ifndef MALLOC_STATIC_PAGESIZE
        case 'P':
          MOZ_ASSERT(gPageSize >= 4_KiB);
          MOZ_ASSERT(gPageSize <= 64_KiB);
          prefix_arg = prefix_arg ? prefix_arg : 1;
          gPageSize <<= prefix_arg;
          // We know that if the shift causes gPageSize to be zero then it's
          // because it shifted all the bits off.  We didn't start with zero.
          // Therefore if gPageSize is out of bounds we set it to 64KiB.
          if (gPageSize < 4_KiB || gPageSize > 64_KiB) {
            gPageSize = 64_KiB;
          }
          break;
#  endif
#endif
        case 'r':
          opt_randomize_small = false;
          break;
        case 'R':
          opt_randomize_small = true;
          break;
        default: {
          char cbuf[2];

          cbuf[0] = opts[i];
          cbuf[1] = '\0';
          _malloc_message(_getprogname(),
                          ": (malloc) Unsupported character "
                          "in malloc options: '",
                          cbuf, "'\n");
        }
      }
    }
  }

#ifndef MALLOC_STATIC_PAGESIZE
  DefineGlobals();
#endif
  gRecycledSize = 0;

  chunks_init();
  huge_init();
  base_init();

  // Initialize arenas collection here.
  if (!gArenas.Init()) {
    return false;
  }

  // Assign the default arena to the initial thread.
  thread_arena.set(gArenas.GetDefault());

  if (!gChunkRTree.Init()) {
    return false;
  }

  malloc_initialized = true;

  // Dummy call so that the function is not removed by dead-code elimination
  Debug::jemalloc_ptr_info(nullptr);

#if !defined(XP_WIN) && !defined(XP_DARWIN)
  // Prevent potential deadlock on malloc locks after fork.
  pthread_atfork(_malloc_prefork, _malloc_postfork_parent,
                 _malloc_postfork_child);
#endif

  return true;
}

// End general internal functions.
// ***************************************************************************
// Begin malloc(3)-compatible functions.

// The BaseAllocator class is a helper class that implements the base allocator
// functions (malloc, calloc, realloc, free, memalign) for a given arena,
// or an appropriately chosen arena (per choose_arena()) when none is given.
struct BaseAllocator {
#define MALLOC_DECL(name, return_type, ...) \
  inline return_type name(__VA_ARGS__);

#define MALLOC_FUNCS MALLOC_FUNCS_MALLOC_BASE
#include "malloc_decls.h"

  explicit BaseAllocator(arena_t* aArena) : mArena(aArena) {}

 private:
  arena_t* mArena;
};

#define MALLOC_DECL(name, return_type, ...)                  \
  inline return_type MozJemalloc::name(                      \
      ARGS_HELPER(TYPED_ARGS, ##__VA_ARGS__)) {              \
    BaseAllocator allocator(nullptr);                        \
    return allocator.name(ARGS_HELPER(ARGS, ##__VA_ARGS__)); \
  }
#define MALLOC_FUNCS MALLOC_FUNCS_MALLOC_BASE
#include "malloc_decls.h"

inline void* BaseAllocator::malloc(size_t aSize) {
  void* ret;
  arena_t* arena;

  if (!malloc_init()) {
    ret = nullptr;
    goto RETURN;
  }

  if (aSize == 0) {
    aSize = 1;
  }
  // If mArena is non-null, it must not be in the first page.
  MOZ_DIAGNOSTIC_ASSERT_IF(mArena, (size_t)mArena >= gPageSize);
  arena = mArena ? mArena : choose_arena(aSize);
  ret = arena->Malloc(aSize, /* aZero = */ false);

RETURN:
  if (!ret) {
    errno = ENOMEM;
  }

  return ret;
}

inline void* BaseAllocator::memalign(size_t aAlignment, size_t aSize) {
  MOZ_ASSERT(((aAlignment - 1) & aAlignment) == 0);

  if (!malloc_init()) {
    return nullptr;
  }

  if (aSize == 0) {
    aSize = 1;
  }

  aAlignment = aAlignment < sizeof(void*) ? sizeof(void*) : aAlignment;
  arena_t* arena = mArena ? mArena : choose_arena(aSize);
  return arena->Palloc(aAlignment, aSize);
}

inline void* BaseAllocator::calloc(size_t aNum, size_t aSize) {
  void* ret;

  if (malloc_init()) {
    CheckedInt<size_t> checkedSize = CheckedInt<size_t>(aNum) * aSize;
    if (checkedSize.isValid()) {
      size_t allocSize = checkedSize.value();
      if (allocSize == 0) {
        allocSize = 1;
      }
      arena_t* arena = mArena ? mArena : choose_arena(allocSize);
      ret = arena->Malloc(allocSize, /* aZero = */ true);
    } else {
      ret = nullptr;
    }
  } else {
    ret = nullptr;
  }

  if (!ret) {
    errno = ENOMEM;
  }

  return ret;
}

inline void* BaseAllocator::realloc(void* aPtr, size_t aSize) {
  void* ret;

  if (aSize == 0) {
    aSize = 1;
  }

  if (aPtr) {
    MOZ_RELEASE_ASSERT(malloc_initialized);

    auto info = AllocInfo::Get(aPtr);
    auto arena = info.Arena();
    MOZ_RELEASE_ASSERT(!mArena || arena == mArena);
    ret = arena->Ralloc(aPtr, aSize, info.Size());
  } else {
    if (!malloc_init()) {
      ret = nullptr;
    } else {
      arena_t* arena = mArena ? mArena : choose_arena(aSize);
      ret = arena->Malloc(aSize, /* aZero = */ false);
    }
  }

  if (!ret) {
    errno = ENOMEM;
  }
  return ret;
}

inline void BaseAllocator::free(void* aPtr) {
  size_t offset;

  // A version of idalloc that checks for nullptr pointer.
  offset = GetChunkOffsetForPtr(aPtr);
  if (offset != 0) {
    MOZ_RELEASE_ASSERT(malloc_initialized);
    arena_dalloc(aPtr, offset, mArena);
  } else if (aPtr) {
    MOZ_RELEASE_ASSERT(malloc_initialized);
    huge_dalloc(aPtr, mArena);
  }
}

inline int MozJemalloc::posix_memalign(void** aMemPtr, size_t aAlignment,
                                       size_t aSize) {
  return AlignedAllocator<memalign>::posix_memalign(aMemPtr, aAlignment, aSize);
}

inline void* MozJemalloc::aligned_alloc(size_t aAlignment, size_t aSize) {
  return AlignedAllocator<memalign>::aligned_alloc(aAlignment, aSize);
}

inline void* MozJemalloc::valloc(size_t aSize) {
  return AlignedAllocator<memalign>::valloc(aSize);
}

// End malloc(3)-compatible functions.
// ***************************************************************************
// Begin non-standard functions.

// This was added by Mozilla for use by SQLite.
inline size_t MozJemalloc::malloc_good_size(size_t aSize) {
  if (aSize <= gMaxLargeClass) {
    // Small or large
    aSize = SizeClass(aSize).Size();
  } else {
    // Huge.  We use PAGE_CEILING to get psize, instead of using
    // CHUNK_CEILING to get csize.  This ensures that this
    // malloc_usable_size(malloc(n)) always matches
    // malloc_good_size(n).
    aSize = PAGE_CEILING(aSize);
  }
  return aSize;
}

inline size_t MozJemalloc::malloc_usable_size(usable_ptr_t aPtr) {
  return AllocInfo::GetValidated(aPtr).Size();
}

inline void MozJemalloc::jemalloc_stats_internal(
    jemalloc_stats_t* aStats, jemalloc_bin_stats_t* aBinStats) {
  size_t non_arena_mapped, chunk_header_size;

  if (!aStats) {
    return;
  }
  if (!malloc_init()) {
    memset(aStats, 0, sizeof(*aStats));
    return;
  }
  if (aBinStats) {
    memset(aBinStats, 0, sizeof(jemalloc_bin_stats_t) * NUM_SMALL_CLASSES);
  }

  // Gather runtime settings.
  aStats->opt_junk = opt_junk;
  aStats->opt_randomize_small = opt_randomize_small;
  aStats->opt_zero = opt_zero;
  aStats->quantum = kQuantum;
  aStats->quantum_max = kMaxQuantumClass;
  aStats->quantum_wide = kQuantumWide;
  aStats->quantum_wide_max = kMaxQuantumWideClass;
  aStats->subpage_max = gMaxSubPageClass;
  aStats->large_max = gMaxLargeClass;
  aStats->chunksize = kChunkSize;
  aStats->page_size = gPageSize;
  aStats->dirty_max = opt_dirty_max;

  // Gather current memory usage statistics.
  aStats->narenas = 0;
  aStats->mapped = 0;
  aStats->allocated = 0;
  aStats->waste = 0;
  aStats->pages_dirty = 0;
  aStats->pages_fresh = 0;
  aStats->pages_madvised = 0;
  aStats->bookkeeping = 0;
  aStats->bin_unused = 0;

  non_arena_mapped = 0;

  // Get huge mapped/allocated.
  {
    MutexAutoLock lock(huge_mtx);
    non_arena_mapped += huge_mapped;
    aStats->allocated += huge_allocated;
    aStats->num_operations += huge_operations;
    MOZ_ASSERT(huge_mapped >= huge_allocated);
  }

  // Get base mapped/allocated.
  {
    MutexAutoLock lock(base_mtx);
    non_arena_mapped += base_mapped;
    aStats->bookkeeping += base_committed;
    MOZ_ASSERT(base_mapped >= base_committed);
  }

  gArenas.mLock.Lock();

  // Stats can only read complete information if its run on the main thread.
  MOZ_ASSERT(gArenas.IsOnMainThreadWeak());

  // Iterate over arenas.
  for (auto arena : gArenas.iter()) {
    // Cannot safely read stats for this arena and therefore stats would be
    // incomplete.
    MOZ_ASSERT(arena->mLock.SafeOnThisThread());

    size_t arena_mapped, arena_allocated, arena_committed, arena_dirty,
        arena_fresh, arena_madvised, j, arena_unused, arena_headers;

    arena_headers = 0;
    arena_unused = 0;

    {
      MaybeMutexAutoLock lock(arena->mLock);

      arena_mapped = arena->mStats.mapped;

      // "committed" counts dirty and allocated memory.
      arena_committed = arena->mStats.committed << gPageSize2Pow;

      arena_allocated =
          arena->mStats.allocated_small + arena->mStats.allocated_large;

      arena_dirty = arena->mNumDirty << gPageSize2Pow;
      arena_fresh = arena->mNumFresh << gPageSize2Pow;
      arena_madvised = arena->mNumMAdvised << gPageSize2Pow;

      aStats->num_operations += arena->mStats.operations;

      for (j = 0; j < NUM_SMALL_CLASSES; j++) {
        arena_bin_t* bin = &arena->mBins[j];
        size_t bin_unused = 0;
        size_t num_non_full_runs = 0;

        for (arena_run_t& run : bin->mNonFullRuns) {
          MOZ_DIAGNOSTIC_ASSERT(run.mMagic == ARENA_RUN_MAGIC);
          MOZ_RELEASE_ASSERT(run.mNumFree > 0 &&
                             run.mNumFree < bin->mRunNumRegions);
          MOZ_RELEASE_ASSERT(run.mBin == bin);
          MOZ_RELEASE_ASSERT(bin->mNonFullRuns.ElementIsLinkedWell(&run));
          arena_chunk_t* chunk = GetChunkForPtr(&run);
          MOZ_RELEASE_ASSERT(chunk->mArena == arena);
          bin_unused += run.mNumFree * bin->mSizeClass;
          num_non_full_runs++;
        }

        arena_unused += bin_unused;
        arena_headers += bin->mNumRuns * bin->mRunFirstRegionOffset;
        if (aBinStats) {
          aBinStats[j].size = bin->mSizeClass;
          aBinStats[j].num_non_full_runs += num_non_full_runs;
          aBinStats[j].num_runs += bin->mNumRuns;
          aBinStats[j].bytes_unused += bin_unused;
          size_t bytes_per_run = static_cast<size_t>(bin->mRunSizePages)
                                 << gPageSize2Pow;
          aBinStats[j].bytes_total +=
              bin->mNumRuns * (bytes_per_run - bin->mRunFirstRegionOffset);
          aBinStats[j].bytes_per_run = bytes_per_run;
          aBinStats[j].regions_per_run = bin->mRunNumRegions;
        }
      }
    }

    MOZ_ASSERT(arena_mapped >= arena_committed);
    MOZ_ASSERT(arena_committed >= arena_allocated + arena_dirty);

    aStats->mapped += arena_mapped;
    aStats->allocated += arena_allocated;
    aStats->pages_dirty += arena_dirty;
    aStats->pages_fresh += arena_fresh;
    aStats->pages_madvised += arena_madvised;
    // "waste" is committed memory that is neither dirty nor
    // allocated.  If you change this definition please update
    // memory/replace/logalloc/replay/Replay.cpp's jemalloc_stats calculation of
    // committed.
    MOZ_ASSERT(arena_committed >=
               (arena_allocated + arena_dirty + arena_unused + arena_headers));
    aStats->waste += arena_committed - arena_allocated - arena_dirty -
                     arena_unused - arena_headers;
    aStats->bin_unused += arena_unused;
    aStats->bookkeeping += arena_headers;
    aStats->narenas++;
  }
  gArenas.mLock.Unlock();

  // Account for arena chunk headers in bookkeeping rather than waste.
  chunk_header_size =
      ((aStats->mapped / aStats->chunksize) * (gChunkHeaderNumPages - 1))
      << gPageSize2Pow;

  aStats->mapped += non_arena_mapped;
  aStats->bookkeeping += chunk_header_size;
  aStats->waste -= chunk_header_size;

  MOZ_ASSERT(aStats->mapped >= aStats->allocated + aStats->waste +
                                   aStats->pages_dirty + aStats->bookkeeping);
}

inline void MozJemalloc::jemalloc_stats_lite(jemalloc_stats_lite_t* aStats) {
  if (!aStats) {
    return;
  }
  if (!malloc_init()) {
    memset(aStats, 0, sizeof(*aStats));
    return;
  }

  aStats->allocated_bytes = 0;
  aStats->num_operations = 0;

  // Get huge mapped/allocated.
  {
    MutexAutoLock lock(huge_mtx);
    aStats->allocated_bytes += huge_allocated;
    aStats->num_operations += huge_operations;
    MOZ_ASSERT(huge_mapped >= huge_allocated);
  }

  {
    MutexAutoLock lock(gArenas.mLock);
    for (auto arena : gArenas.iter()) {
      // We don't need to lock the arena to access these fields.
      aStats->allocated_bytes += arena->AllocatedBytes();
      aStats->num_operations += arena->Operations();
    }
    aStats->num_operations += gArenas.OperationsDisposedArenas();
  }
}

inline size_t MozJemalloc::jemalloc_stats_num_bins() {
  return NUM_SMALL_CLASSES;
}

inline void MozJemalloc::jemalloc_set_main_thread() {
  MOZ_ASSERT(malloc_initialized);
  gArenas.SetMainThread();
}

#ifdef MALLOC_DOUBLE_PURGE

// Explicitly remove all of this chunk's MADV_FREE'd pages from memory.
static size_t hard_purge_chunk(arena_chunk_t* aChunk) {
  size_t total_npages = 0;
  // See similar logic in arena_t::Purge().
  for (size_t i = gChunkHeaderNumPages; i < gChunkNumPages; i++) {
    // Find all adjacent pages with CHUNK_MAP_MADVISED set.
    size_t npages;
    for (npages = 0; aChunk->mPageMap[i + npages].bits & CHUNK_MAP_MADVISED &&
                     i + npages < gChunkNumPages;
         npages++) {
      // Turn off the page's CHUNK_MAP_MADVISED bit and turn on its
      // CHUNK_MAP_FRESH bit.
      MOZ_DIAGNOSTIC_ASSERT(!(aChunk->mPageMap[i + npages].bits &
                              (CHUNK_MAP_FRESH | CHUNK_MAP_DECOMMITTED)));
      aChunk->mPageMap[i + npages].bits ^=
          (CHUNK_MAP_MADVISED | CHUNK_MAP_FRESH);
    }

    // We could use mincore to find out which pages are actually
    // present, but it's not clear that's better.
    if (npages > 0) {
      pages_decommit(((char*)aChunk) + (i << gPageSize2Pow),
                     npages << gPageSize2Pow);
      Unused << pages_commit(((char*)aChunk) + (i << gPageSize2Pow),
                             npages << gPageSize2Pow);
    }
    total_npages += npages;
    i += npages;
  }

  return total_npages;
}

// Explicitly remove all of this arena's MADV_FREE'd pages from memory.
void arena_t::HardPurge() {
  MaybeMutexAutoLock lock(mLock);

  while (!mChunksMAdvised.isEmpty()) {
    arena_chunk_t* chunk = mChunksMAdvised.popFront();
    size_t npages = hard_purge_chunk(chunk);
    mNumMAdvised -= npages;
    mNumFresh += npages;
  }
}

inline void MozJemalloc::jemalloc_purge_freed_pages() {
  if (malloc_initialized) {
    MutexAutoLock lock(gArenas.mLock);
    MOZ_ASSERT(gArenas.IsOnMainThreadWeak());
    for (auto arena : gArenas.iter()) {
      arena->HardPurge();
    }
  }
}

#else  // !defined MALLOC_DOUBLE_PURGE

inline void MozJemalloc::jemalloc_purge_freed_pages() {
  // Do nothing.
}

#endif  // defined MALLOC_DOUBLE_PURGE

inline void MozJemalloc::jemalloc_free_dirty_pages(void) {
  if (malloc_initialized) {
    gArenas.MayPurgeAll(PurgeUnconditional, __func__);
  }
}

inline void MozJemalloc::jemalloc_free_excess_dirty_pages(void) {
  if (malloc_initialized) {
    gArenas.MayPurgeAll(PurgeIfThreshold, __func__);
  }
}

#ifndef NON_RANDOM_ARENA_IDS
inline arena_t* ArenaCollection::GetByIdInternal(Tree& aTree,
                                                 arena_id_t aArenaId) {
  // Use AlignedStorage2 to avoid running the arena_t constructor, while
  // we only need it as a placeholder for mId.
  mozilla::AlignedStorage2<arena_t> key;
  key.addr()->mId = aArenaId;
  return aTree.Search(key.addr());
}
#endif

inline arena_t* ArenaCollection::GetById(arena_id_t aArenaId, bool aIsPrivate) {
  if (!malloc_initialized) {
    return nullptr;
  }

#ifdef NON_RANDOM_ARENA_IDS
  // This function is never called with aIsPrivate = false, let's make sure it
  // doesn't silently change while we're making that assumption below because
  // we can't resolve non-private arenas this way.
  MOZ_RELEASE_ASSERT(aIsPrivate);
  // This function is not expected to be called before at least one private
  // arena was created.
  // coverity[missing_lock]
  MOZ_RELEASE_ASSERT(mArenaIdKey);
  arena_id_t id = (aArenaId << mArenaIdRotation) |
                  (aArenaId >> (sizeof(void*) * 8 - mArenaIdRotation));
  arena_t* result = reinterpret_cast<arena_t*>(id ^ mArenaIdKey);
#else
  Tree* tree = nullptr;
  if (aIsPrivate) {
    if (ArenaIdIsMainThreadOnly(aArenaId)) {
      // The main thread only arenas support lock free access, so it's desirable
      // to do GetById without taking mLock either.
      //
      // Races can occur between writers and writers, or between writers and
      // readers.  The only writer is the main thread and it will never race
      // against itself so we can elude the lock when the main thread is
      // reading.
      MOZ_ASSERT(IsOnMainThread());
      MOZ_PUSH_IGNORE_THREAD_SAFETY
      arena_t* result = GetByIdInternal(mMainThreadArenas, aArenaId);
      MOZ_POP_THREAD_SAFETY
      MOZ_RELEASE_ASSERT(result);
      return result;
    }
    tree = &mPrivateArenas;
  } else {
    tree = &mArenas;
  }

  MutexAutoLock lock(mLock);
  arena_t* result = GetByIdInternal(*tree, aArenaId);
#endif
  MOZ_RELEASE_ASSERT(result);
  MOZ_RELEASE_ASSERT(result->mId == aArenaId);
  return result;
}

inline arena_id_t MozJemalloc::moz_create_arena_with_params(
    arena_params_t* aParams) {
  if (malloc_init()) {
    arena_t* arena = gArenas.CreateArena(/* IsPrivate = */ true, aParams);
    return arena->mId;
  }
  return 0;
}

inline void MozJemalloc::moz_dispose_arena(arena_id_t aArenaId) {
  arena_t* arena = gArenas.GetById(aArenaId, /* IsPrivate = */ true);
  MOZ_RELEASE_ASSERT(arena);
  gArenas.DisposeArena(arena);
}

inline void MozJemalloc::moz_set_max_dirty_page_modifier(int32_t aModifier) {
  if (malloc_init()) {
    gArenas.SetDefaultMaxDirtyPageModifier(aModifier);
  }
}

inline void MozJemalloc::jemalloc_reset_small_alloc_randomization(
    bool aRandomizeSmall) {
  // When this process got forked by ForkServer then it inherited the existing
  // state of mozjemalloc. Specifically, parsing of MALLOC_OPTIONS has already
  // been done but it may not reflect anymore the current set of options after
  // the fork().
  //
  // Similar behavior is also present on Android where it is also required to
  // perform this step.
  //
  // Content process will have randomization on small malloc disabled via the
  // MALLOC_OPTIONS environment variable set by parent process, missing this
  // will lead to serious performance regressions because CPU prefetch will
  // break, cf bug 1912262.  However on forkserver-forked Content processes, the
  // environment is not yet reset when the postfork child handler is being
  // called.
  //
  // This API is here to allow those Content processes (spawned by ForkServer or
  // Android service) to notify jemalloc to turn off the randomization on small
  // allocations and perform the required reinitialization of already existing
  // arena's PRNG.  It is important to make sure that the PRNG state is properly
  // re-initialized otherwise child processes would share all the same state.

  {
    AutoLock<StaticMutex> lock(gInitLock);
    opt_randomize_small = aRandomizeSmall;
  }

  MutexAutoLock lock(gArenas.mLock);
  for (auto* arena : gArenas.iter()) {
    // We can only initialize the PRNG for main-thread-only arenas from the main
    // thread.
    if (!arena->IsMainThreadOnly() || gArenas.IsOnMainThreadWeak()) {
      arena->ResetSmallAllocRandomization();
    }
  }
}

inline bool MozJemalloc::moz_enable_deferred_purge(bool aEnabled) {
  return gArenas.SetDeferredPurge(aEnabled);
}

inline may_purge_now_result_t MozJemalloc::moz_may_purge_now(
    bool aPeekOnly, uint32_t aReuseGraceMS,
    const Maybe<std::function<bool()>>& aKeepGoing) {
  return gArenas.MayPurgeSteps(aPeekOnly, aReuseGraceMS, aKeepGoing);
}

inline void ArenaCollection::AddToOutstandingPurges(arena_t* aArena) {
  MOZ_ASSERT(aArena);

  // We cannot trust the caller to know whether the element was already added
  // from another thread given we have our own lock.
  MutexAutoLock lock(mPurgeListLock);
  if (!mOutstandingPurges.ElementProbablyInList(aArena)) {
    mOutstandingPurges.pushBack(aArena);
  }
}

inline bool ArenaCollection::RemoveFromOutstandingPurges(arena_t* aArena) {
  MOZ_ASSERT(aArena);

  // We cannot trust the caller to know whether the element was already removed
  // from another thread given we have our own lock.
  MutexAutoLock lock(mPurgeListLock);
  if (mOutstandingPurges.ElementProbablyInList(aArena)) {
    mOutstandingPurges.remove(aArena);
    return true;
  }
  return false;
}

may_purge_now_result_t ArenaCollection::MayPurgeSteps(
    bool aPeekOnly, uint32_t aReuseGraceMS,
    const Maybe<std::function<bool()>>& aKeepGoing) {
  // This only works on the main thread because it may process main-thread-only
  // arenas.
  MOZ_ASSERT(IsOnMainThreadWeak());

  uint64_t now = GetTimestampNS();
  uint64_t reuseGraceNS = (uint64_t)aReuseGraceMS * 1000 * 1000;
  arena_t* found = nullptr;
  {
    MutexAutoLock lock(mPurgeListLock);
    if (mOutstandingPurges.isEmpty()) {
      return may_purge_now_result_t::Done;
    }
    for (arena_t& arena : mOutstandingPurges) {
      if (now - arena.mLastSignificantReuseNS >= reuseGraceNS) {
        found = &arena;
        break;
      }
    }

    if (!found) {
      return may_purge_now_result_t::WantsLater;
    }
    if (aPeekOnly) {
      return may_purge_now_result_t::NeedsMore;
    }

    // We need to avoid the invalid state where mIsDeferredPurgePending is set
    // but the arena is not in the list or about to be added. So remove the
    // arena from the list before calling Purge().
    mOutstandingPurges.remove(found);
  }

  ArenaPurgeResult pr =
      found->PurgeLoop(PurgeIfThreshold, __func__, aReuseGraceMS, aKeepGoing);

  if (pr == ArenaPurgeResult::NotDone) {
    // If there's more work to do we re-insert the arena into the purge queue.
    // If the arena was busy we don't since the other thread that's purging it
    // will finish that work.

    // Note that after the above Purge() and taking the lock below there's a
    // chance another thread may be purging the arena and clear
    // mIsDeferredPurgePending.  Resulting in the state of being in the list
    // with that flag clear.  That's okay since the next time a purge occurs
    // (and one will because it's in the list) it'll clear the flag and the
    // state will be consistent again.
    MutexAutoLock lock(mPurgeListLock);
    if (!mOutstandingPurges.ElementProbablyInList(found)) {
      // Given we want to continue to purge this arena, push it to the front
      // to increase the probability to find it fast.
      mOutstandingPurges.pushFront(found);
    }
  } else if (pr == ArenaPurgeResult::Dying) {
    delete found;
  }

  // Even if there is no other arena that needs work, let the caller just call
  // us again and we will do the above checks then and return their result.
  // Note that in the current surrounding setting this may (rarely) cause a
  // new slice of our idle task runner if we are exceeding idle budget.
  return may_purge_now_result_t::NeedsMore;
}

void ArenaCollection::MayPurgeAll(PurgeCondition aCond, const char* aCaller) {
  MutexAutoLock lock(mLock);
  for (auto* arena : iter()) {
    // Arenas that are not IsMainThreadOnly can be purged from any thread.
    // So we do what we can even if called from another thread.
    if (!arena->IsMainThreadOnly() || IsOnMainThreadWeak()) {
      RemoveFromOutstandingPurges(arena);
      ArenaPurgeResult pr = arena->PurgeLoop(aCond, aCaller);

      // No arena can die here because we're holding the arena collection lock.
      // Arenas are removed from the collection before setting their mDying
      // flag.
      MOZ_RELEASE_ASSERT(pr != ArenaPurgeResult::Dying);
    }
  }
}

#define MALLOC_DECL(name, return_type, ...)                          \
  inline return_type MozJemalloc::moz_arena_##name(                  \
      arena_id_t aArenaId, ARGS_HELPER(TYPED_ARGS, ##__VA_ARGS__)) { \
    BaseAllocator allocator(                                         \
        gArenas.GetById(aArenaId, /* IsPrivate = */ true));          \
    return allocator.name(ARGS_HELPER(ARGS, ##__VA_ARGS__));         \
  }
#define MALLOC_FUNCS MALLOC_FUNCS_MALLOC_BASE
#include "malloc_decls.h"

// End non-standard functions.
// ***************************************************************************
#ifndef XP_WIN
// Begin library-private functions, used by threading libraries for protection
// of malloc during fork().  These functions are only called if the program is
// running in threaded mode, so there is no need to check whether the program
// is threaded here.
//
// Note that the only way to keep the main-thread-only arenas in a consistent
// state for the child is if fork is called from the main thread only.  Or the
// child must not use them, eg it should call exec().  We attempt to prevent the
// child for accessing these arenas by refusing to re-initialise them.
//
// This is only accessed in the fork handlers while gArenas.mLock is held.
static pthread_t gForkingThread;

#  ifdef XP_DARWIN
// This is only accessed in the fork handlers while gArenas.mLock is held.
static pid_t gForkingProcess;
#  endif

FORK_HOOK
void _malloc_prefork(void) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  // Acquire all mutexes in a safe order.
  gArenas.mLock.Lock();
  gForkingThread = pthread_self();
#  ifdef XP_DARWIN
  gForkingProcess = getpid();
#  endif

  for (auto arena : gArenas.iter()) {
    if (arena->mLock.LockIsEnabled()) {
      arena->mLock.Lock();
    }
  }

  gArenas.mPurgeListLock.Lock();

  base_mtx.Lock();

  huge_mtx.Lock();
}

FORK_HOOK
void _malloc_postfork_parent(void) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  // Release all mutexes, now that fork() has completed.
  huge_mtx.Unlock();

  base_mtx.Unlock();

  gArenas.mPurgeListLock.Unlock();

  for (auto arena : gArenas.iter()) {
    if (arena->mLock.LockIsEnabled()) {
      arena->mLock.Unlock();
    }
  }

  gArenas.mLock.Unlock();
}

FORK_HOOK
void _malloc_postfork_child(void) {
  // Do this before iterating over the arenas.
  gArenas.ResetMainThread();

  // Reinitialize all mutexes, now that fork() has completed.
  huge_mtx.Init();

  base_mtx.Init();

  gArenas.mPurgeListLock.Init();

  MOZ_PUSH_IGNORE_THREAD_SAFETY
  for (auto arena : gArenas.iter()) {
    arena->mLock.Reinit(gForkingThread);
  }
  MOZ_POP_THREAD_SAFETY

  gArenas.mLock.Init();
}

#  ifdef XP_DARWIN
FORK_HOOK
void _malloc_postfork(void) {
  // On MacOS we need to check if this is running in the parent or child
  // process.
  bool is_in_parent = getpid() == gForkingProcess;
  gForkingProcess = 0;
  if (is_in_parent) {
    _malloc_postfork_parent();
  } else {
    _malloc_postfork_child();
  }
}
#  endif  // XP_DARWIN
#endif    // ! XP_WIN

// End library-private functions.
// ***************************************************************************
#ifdef MOZ_REPLACE_MALLOC
// Windows doesn't come with weak imports as they are possible with
// LD_PRELOAD or DYLD_INSERT_LIBRARIES on Linux/OSX. On this platform,
// the replacement functions are defined as variable pointers to the
// function resolved with GetProcAddress() instead of weak definitions
// of functions. On Android, the same needs to happen as well, because
// the Android linker doesn't handle weak linking with non LD_PRELOADed
// libraries, but LD_PRELOADing is not very convenient on Android, with
// the zygote.
#  ifdef XP_DARWIN
#    define MOZ_REPLACE_WEAK __attribute__((weak_import))
#  elif defined(XP_WIN) || defined(ANDROID)
#    define MOZ_DYNAMIC_REPLACE_INIT
#    define replace_init replace_init_decl
#  elif defined(__GNUC__)
#    define MOZ_REPLACE_WEAK __attribute__((weak))
#  endif

#  include "replace_malloc.h"

#  define MALLOC_DECL(name, return_type, ...) CanonicalMalloc::name,

// The default malloc table, i.e. plain allocations. It never changes. It's
// used by init(), and not used after that.
static const malloc_table_t gDefaultMallocTable = {
#  include "malloc_decls.h"
};

// The malloc table installed by init(). It never changes from that point
// onward. It will be the same as gDefaultMallocTable if no replace-malloc tool
// is enabled at startup.
static malloc_table_t gOriginalMallocTable = {
#  include "malloc_decls.h"
};

// The malloc table installed by jemalloc_replace_dynamic(). (Read the
// comments above that function for more details.)
static malloc_table_t gDynamicMallocTable = {
#  include "malloc_decls.h"
};

// This briefly points to gDefaultMallocTable at startup. After that, it points
// to either gOriginalMallocTable or gDynamicMallocTable. It's atomic to avoid
// races when switching between tables.
static Atomic<malloc_table_t const*, mozilla::MemoryOrdering::Relaxed>
    gMallocTablePtr;

#  ifdef MOZ_DYNAMIC_REPLACE_INIT
#    undef replace_init
typedef decltype(replace_init_decl) replace_init_impl_t;
static replace_init_impl_t* replace_init = nullptr;
#  endif

#  ifdef XP_WIN
typedef HMODULE replace_malloc_handle_t;

static replace_malloc_handle_t replace_malloc_handle() {
  wchar_t replace_malloc_lib[1024];
  if (GetEnvironmentVariableW(L"MOZ_REPLACE_MALLOC_LIB", replace_malloc_lib,
                              std::size(replace_malloc_lib)) > 0) {
    return LoadLibraryW(replace_malloc_lib);
  }
  return nullptr;
}

#    define REPLACE_MALLOC_GET_INIT_FUNC(handle) \
      (replace_init_impl_t*)GetProcAddress(handle, "replace_init")

#  elif defined(ANDROID)
#    include <dlfcn.h>

typedef void* replace_malloc_handle_t;

static replace_malloc_handle_t replace_malloc_handle() {
  const char* replace_malloc_lib = getenv("MOZ_REPLACE_MALLOC_LIB");
  if (replace_malloc_lib && *replace_malloc_lib) {
    return dlopen(replace_malloc_lib, RTLD_LAZY);
  }
  return nullptr;
}

#    define REPLACE_MALLOC_GET_INIT_FUNC(handle) \
      (replace_init_impl_t*)dlsym(handle, "replace_init")

#  endif

static void replace_malloc_init_funcs(malloc_table_t*);

#  ifdef MOZ_REPLACE_MALLOC_STATIC
extern "C" void logalloc_init(malloc_table_t*, ReplaceMallocBridge**);

extern "C" void dmd_init(malloc_table_t*, ReplaceMallocBridge**);
#  endif

void phc_init(malloc_table_t*, ReplaceMallocBridge**);

bool Equals(const malloc_table_t& aTable1, const malloc_table_t& aTable2) {
  return memcmp(&aTable1, &aTable2, sizeof(malloc_table_t)) == 0;
}

// Below is the malloc implementation overriding jemalloc and calling the
// replacement functions if they exist.
static ReplaceMallocBridge* gReplaceMallocBridge = nullptr;
static void init() {
  malloc_table_t tempTable = gDefaultMallocTable;

#  ifdef MOZ_DYNAMIC_REPLACE_INIT
  replace_malloc_handle_t handle = replace_malloc_handle();
  if (handle) {
    replace_init = REPLACE_MALLOC_GET_INIT_FUNC(handle);
  }
#  endif

  // Set this *before* calling replace_init, otherwise if replace_init calls
  // malloc() we'll get an infinite loop.
  gMallocTablePtr = &gDefaultMallocTable;

  // Pass in the default allocator table so replace functions can copy and use
  // it for their allocations. The replace_init() function should modify the
  // table if it wants to be active, otherwise leave it unmodified.
  if (replace_init) {
    replace_init(&tempTable, &gReplaceMallocBridge);
  }
#  ifdef MOZ_REPLACE_MALLOC_STATIC
  if (Equals(tempTable, gDefaultMallocTable)) {
    logalloc_init(&tempTable, &gReplaceMallocBridge);
  }
#    ifdef MOZ_DMD
  if (Equals(tempTable, gDefaultMallocTable)) {
    dmd_init(&tempTable, &gReplaceMallocBridge);
  }
#    endif
#  endif
  if (!Equals(tempTable, gDefaultMallocTable)) {
    replace_malloc_init_funcs(&tempTable);
  }
  gOriginalMallocTable = tempTable;
  gMallocTablePtr = &gOriginalMallocTable;
}

// WARNING WARNING WARNING: this function should be used with extreme care. It
// is not as general-purpose as it looks. It is currently used by
// tools/profiler/core/memory_hooks.cpp for counting allocations and probably
// should not be used for any other purpose.
//
// This function allows the original malloc table to be temporarily replaced by
// a different malloc table. Or, if the argument is nullptr, it switches back to
// the original malloc table.
//
// Limitations:
//
// - It is not threadsafe. If multiple threads pass it the same
//   `replace_init_func` at the same time, there will be data races writing to
//   the malloc_table_t within that function.
//
// - Only one replacement can be installed. No nesting is allowed.
//
// - The new malloc table must be able to free allocations made by the original
//   malloc table, and upon removal the original malloc table must be able to
//   free allocations made by the new malloc table. This means the new malloc
//   table can only do simple things like recording extra information, while
//   delegating actual allocation/free operations to the original malloc table.
//
MOZ_JEMALLOC_API void jemalloc_replace_dynamic(
    jemalloc_init_func replace_init_func) {
  if (replace_init_func) {
    malloc_table_t tempTable = gOriginalMallocTable;
    (*replace_init_func)(&tempTable, &gReplaceMallocBridge);
    if (!Equals(tempTable, gOriginalMallocTable)) {
      replace_malloc_init_funcs(&tempTable);

      // Temporarily switch back to the original malloc table. In the
      // (supported) non-nested case, this is a no-op. But just in case this is
      // a (unsupported) nested call, it makes the overwriting of
      // gDynamicMallocTable less racy, because ongoing calls to malloc() and
      // friends won't go through gDynamicMallocTable.
      gMallocTablePtr = &gOriginalMallocTable;

      gDynamicMallocTable = tempTable;
      gMallocTablePtr = &gDynamicMallocTable;
      // We assume that dynamic replaces don't occur close enough for a
      // thread to still have old copies of the table pointer when the 2nd
      // replace occurs.
    }
  } else {
    // Switch back to the original malloc table.
    gMallocTablePtr = &gOriginalMallocTable;
  }
}

#  define MALLOC_DECL(name, return_type, ...)                           \
    inline return_type ReplaceMalloc::name(                             \
        ARGS_HELPER(TYPED_ARGS, ##__VA_ARGS__)) {                       \
      if (MOZ_UNLIKELY(!gMallocTablePtr)) {                             \
        init();                                                         \
      }                                                                 \
      return (*gMallocTablePtr).name(ARGS_HELPER(ARGS, ##__VA_ARGS__)); \
    }
#  include "malloc_decls.h"

MOZ_JEMALLOC_API struct ReplaceMallocBridge* get_bridge(void) {
  if (MOZ_UNLIKELY(!gMallocTablePtr)) {
    init();
  }
  return gReplaceMallocBridge;
}

// posix_memalign, aligned_alloc, memalign and valloc all implement some kind
// of aligned memory allocation. For convenience, a replace-malloc library can
// skip defining replace_posix_memalign, replace_aligned_alloc and
// replace_valloc, and default implementations will be automatically derived
// from replace_memalign.
static void replace_malloc_init_funcs(malloc_table_t* table) {
  if (table->posix_memalign == CanonicalMalloc::posix_memalign &&
      table->memalign != CanonicalMalloc::memalign) {
    table->posix_memalign =
        AlignedAllocator<ReplaceMalloc::memalign>::posix_memalign;
  }
  if (table->aligned_alloc == CanonicalMalloc::aligned_alloc &&
      table->memalign != CanonicalMalloc::memalign) {
    table->aligned_alloc =
        AlignedAllocator<ReplaceMalloc::memalign>::aligned_alloc;
  }
  if (table->valloc == CanonicalMalloc::valloc &&
      table->memalign != CanonicalMalloc::memalign) {
    table->valloc = AlignedAllocator<ReplaceMalloc::memalign>::valloc;
  }
  if (table->moz_create_arena_with_params ==
          CanonicalMalloc::moz_create_arena_with_params &&
      table->malloc != CanonicalMalloc::malloc) {
#  define MALLOC_DECL(name, ...) \
    table->name = DummyArenaAllocator<ReplaceMalloc>::name;
#  define MALLOC_FUNCS MALLOC_FUNCS_ARENA_BASE
#  include "malloc_decls.h"
  }
  if (table->moz_arena_malloc == CanonicalMalloc::moz_arena_malloc &&
      table->malloc != CanonicalMalloc::malloc) {
#  define MALLOC_DECL(name, ...) \
    table->name = DummyArenaAllocator<ReplaceMalloc>::name;
#  define MALLOC_FUNCS MALLOC_FUNCS_ARENA_ALLOC
#  include "malloc_decls.h"
  }
}

#endif  // MOZ_REPLACE_MALLOC
// ***************************************************************************
// Definition of all the _impl functions
// GENERIC_MALLOC_DECL2_MINGW is only used for the MinGW build, and aliases
// the malloc funcs (e.g. malloc) to the je_ versions. It does not generate
// aliases for the other functions (jemalloc and arena functions).
//
// We do need aliases for the other mozglue.def-redirected functions though,
// these are done at the bottom of mozmemory_wrap.cpp
#define GENERIC_MALLOC_DECL2_MINGW(name, name_impl, return_type, ...) \
  return_type name(ARGS_HELPER(TYPED_ARGS, ##__VA_ARGS__))            \
      __attribute__((alias(MOZ_STRINGIFY(name_impl))));

#define GENERIC_MALLOC_DECL2(attributes, name, name_impl, return_type, ...)  \
  return_type name_impl(ARGS_HELPER(TYPED_ARGS, ##__VA_ARGS__)) attributes { \
    return DefaultMalloc::name(ARGS_HELPER(ARGS, ##__VA_ARGS__));            \
  }

#ifndef __MINGW32__
#  define GENERIC_MALLOC_DECL(attributes, name, return_type, ...)    \
    GENERIC_MALLOC_DECL2(attributes, name, name##_impl, return_type, \
                         ##__VA_ARGS__)
#else
#  define GENERIC_MALLOC_DECL(attributes, name, return_type, ...)    \
    GENERIC_MALLOC_DECL2(attributes, name, name##_impl, return_type, \
                         ##__VA_ARGS__)                              \
    GENERIC_MALLOC_DECL2_MINGW(name, name##_impl, return_type, ##__VA_ARGS__)
#endif

#define NOTHROW_MALLOC_DECL(...) \
  MOZ_MEMORY_API MACRO_CALL(GENERIC_MALLOC_DECL, (noexcept(true), __VA_ARGS__))
#define MALLOC_DECL(...) \
  MOZ_MEMORY_API MACRO_CALL(GENERIC_MALLOC_DECL, (, __VA_ARGS__))
#define MALLOC_FUNCS MALLOC_FUNCS_MALLOC
#include "malloc_decls.h"

#undef GENERIC_MALLOC_DECL
#define GENERIC_MALLOC_DECL(attributes, name, return_type, ...) \
  GENERIC_MALLOC_DECL2(attributes, name, name, return_type, ##__VA_ARGS__)

#define MALLOC_DECL(...) \
  MOZ_JEMALLOC_API MACRO_CALL(GENERIC_MALLOC_DECL, (, __VA_ARGS__))
#define MALLOC_FUNCS (MALLOC_FUNCS_JEMALLOC | MALLOC_FUNCS_ARENA)
#include "malloc_decls.h"
// ***************************************************************************

#ifdef HAVE_DLFCN_H
#  include <dlfcn.h>
#endif

#if defined(__GLIBC__) && !defined(__UCLIBC__)
// glibc provides the RTLD_DEEPBIND flag for dlopen which can make it possible
// to inconsistently reference libc's malloc(3)-compatible functions
// (bug 493541).
//
// These definitions interpose hooks in glibc.  The functions are actually
// passed an extra argument for the caller return address, which will be
// ignored.

extern "C" {
MOZ_EXPORT void (*__free_hook)(void*) = free_impl;
MOZ_EXPORT void* (*__malloc_hook)(size_t) = malloc_impl;
MOZ_EXPORT void* (*__realloc_hook)(void*, size_t) = realloc_impl;
MOZ_EXPORT void* (*__memalign_hook)(size_t, size_t) = memalign_impl;
}

#elif defined(RTLD_DEEPBIND)
// XXX On systems that support RTLD_GROUP or DF_1_GROUP, do their
// implementations permit similar inconsistencies?  Should STV_SINGLETON
// visibility be used for interposition where available?
#  error \
      "Interposing malloc is unsafe on this system without libc malloc hooks."
#endif

#ifdef XP_WIN
MOZ_EXPORT void* _recalloc(void* aPtr, size_t aCount, size_t aSize) {
  size_t oldsize = aPtr ? AllocInfo::Get(aPtr).Size() : 0;
  CheckedInt<size_t> checkedSize = CheckedInt<size_t>(aCount) * aSize;

  if (!checkedSize.isValid()) {
    return nullptr;
  }

  size_t newsize = checkedSize.value();

  // In order for all trailing bytes to be zeroed, the caller needs to
  // use calloc(), followed by recalloc().  However, the current calloc()
  // implementation only zeros the bytes requested, so if recalloc() is
  // to work 100% correctly, calloc() will need to change to zero
  // trailing bytes.
  aPtr = DefaultMalloc::realloc(aPtr, newsize);
  if (aPtr && oldsize < newsize) {
    memset((void*)((uintptr_t)aPtr + oldsize), 0, newsize - oldsize);
  }

  return aPtr;
}

// This impl of _expand doesn't ever actually expand or shrink blocks: it
// simply replies that you may continue using a shrunk block.
MOZ_EXPORT void* _expand(void* aPtr, size_t newsize) {
  if (AllocInfo::Get(aPtr).Size() >= newsize) {
    return aPtr;
  }

  return nullptr;
}

MOZ_EXPORT size_t _msize(void* aPtr) {
  return DefaultMalloc::malloc_usable_size(aPtr);
}
#endif

#ifdef MOZ_PHC
// Compile PHC and mozjemalloc together so that PHC can inline mozjemalloc.
#  include "PHC.cpp"
#endif
