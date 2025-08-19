/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef CHUNK_H
#define CHUNK_H

#include "mozilla/Atomics.h"

#include "RadixTree.h"
#include "RedBlackTree.h"

#include "mozilla/DoublyLinkedList.h"

// ***************************************************************************
// Structures for chunk headers for chunks used for non-huge allocations.

struct arena_t;

enum ChunkType {
  UNKNOWN_CHUNK,
  ZEROED_CHUNK,    // chunk only contains zeroes.
  ARENA_CHUNK,     // used to back arena runs created by arena_t::AllocRun.
  HUGE_CHUNK,      // used to back huge allocations (e.g. arena_t::MallocHuge).
  RECYCLED_CHUNK,  // chunk has been stored for future use by chunk_recycle.
};

// Each element of the chunk map corresponds to one page within the chunk.
struct arena_chunk_map_t {
  // Linkage for run trees. Used for arena_t's tree or available runs.
  RedBlackTreeNode<arena_chunk_map_t> link;

  // Run address (or size) and various flags are stored together.  The bit
  // layout looks like (assuming 32-bit system):
  //
  //   ???????? ???????? ????---b fmckdzla
  //
  // ? : Unallocated: Run address for first/last pages, unset for internal
  //                  pages.
  //     Small: Run address.
  //     Large: Run size for first page, unset for trailing pages.
  // - : Unused.
  // b : Busy?
  // f : Fresh memory?
  // m : MADV_FREE/MADV_DONTNEED'ed?
  // c : decommitted?
  // k : key?
  // d : dirty?
  // z : zeroed?
  // l : large?
  // a : allocated?
  //
  // Following are example bit patterns for consecutive pages from the three
  // types of runs.
  //
  // r : run address
  // s : run size
  // x : don't care
  // - : 0
  // [cdzla] : bit set
  //
  //   Unallocated:
  //     ssssssss ssssssss ssss---- --c-----
  //     xxxxxxxx xxxxxxxx xxxx---- ----d---
  //     ssssssss ssssssss ssss---- -----z--
  //
  //     Note that the size fields are set for the first and last unallocated
  //     page only.  The pages in-between have invalid/"don't care" size fields,
  //     they're not cleared during things such as coalescing free runs.
  //
  //     Pages before the first or after the last page in a free run must be
  //     allocated or busy.  Run coalescing depends on the sizes being set in
  //     the first and last page.  Purging pages and releasing chunks require
  //     that unallocated pages are always coalesced and the first page has a
  //     correct size.
  //
  //   Small:
  //     rrrrrrrr rrrrrrrr rrrr---- -------a
  //     rrrrrrrr rrrrrrrr rrrr---- -------a
  //     rrrrrrrr rrrrrrrr rrrr---- -------a
  //
  //   Large:
  //     ssssssss ssssssss ssss---- ------la
  //     -------- -------- -------- ------la
  //     -------- -------- -------- ------la
  //
  //     Note that only the first page has the size set.
  //
  size_t bits;

// A page can be in one of several states.
//
// CHUNK_MAP_ALLOCATED marks allocated pages, the only other bit that can be
// combined is CHUNK_MAP_LARGE.
//
// CHUNK_MAP_LARGE may be combined with CHUNK_MAP_ALLOCATED to show that the
// allocation is a "large" allocation (see SizeClass), rather than a run of
// small allocations.  The interpretation of the gPageSizeMask bits depends onj
// this bit, see the description above.
//
// CHUNK_MAP_DIRTY is used to mark pages that were allocated and are now freed.
// They may contain their previous contents (or poison).  CHUNK_MAP_DIRTY, when
// set, must be the only set bit.
//
// CHUNK_MAP_MADVISED marks pages which are madvised (with either MADV_DONTNEED
// or MADV_FREE).  This is only valid if MALLOC_DECOMMIT is not defined.  When
// set, it must be the only bit set.
//
// CHUNK_MAP_DECOMMITTED is used if CHUNK_MAP_DECOMMITTED is defined.  Unused
// dirty pages may be decommitted and marked as CHUNK_MAP_DECOMMITTED.  They
// must be re-committed with pages_commit() before they can be touched.
//
// CHUNK_MAP_FRESH is set on pages that have never been used before (the chunk
// is newly allocated or they were decommitted and have now been recommitted.
// CHUNK_MAP_FRESH is also used for "double purged" pages meaning that they were
// madvised and later were unmapped and remapped to force them out of the
// program's resident set.  This is enabled when MALLOC_DOUBLE_PURGE is defined
// (eg on MacOS).
//
// CHUNK_MAP_BUSY is set by a thread when the thread wants to manipulate the
// pages without holding a lock. Other threads must not touch these pages
// regardless of whether they hold a lock.
//
// CHUNK_MAP_ZEROED is set on pages that are known to contain zeros.
//
// CHUNK_MAP_DIRTY, _DECOMMITED _MADVISED and _FRESH are always mutually
// exclusive.
//
// CHUNK_MAP_KEY is never used on real pages, only on lookup keys.
//
#define CHUNK_MAP_BUSY ((size_t)0x100U)
#define CHUNK_MAP_FRESH ((size_t)0x80U)
#define CHUNK_MAP_MADVISED ((size_t)0x40U)
#define CHUNK_MAP_DECOMMITTED ((size_t)0x20U)
#define CHUNK_MAP_MADVISED_OR_DECOMMITTED \
  (CHUNK_MAP_MADVISED | CHUNK_MAP_DECOMMITTED)
#define CHUNK_MAP_FRESH_MADVISED_OR_DECOMMITTED \
  (CHUNK_MAP_FRESH | CHUNK_MAP_MADVISED | CHUNK_MAP_DECOMMITTED)
#define CHUNK_MAP_FRESH_MADVISED_DECOMMITTED_OR_BUSY              \
  (CHUNK_MAP_FRESH | CHUNK_MAP_MADVISED | CHUNK_MAP_DECOMMITTED | \
   CHUNK_MAP_BUSY)
#define CHUNK_MAP_KEY ((size_t)0x10U)
#define CHUNK_MAP_DIRTY ((size_t)0x08U)
#define CHUNK_MAP_ZEROED ((size_t)0x04U)
#define CHUNK_MAP_LARGE ((size_t)0x02U)
#define CHUNK_MAP_ALLOCATED ((size_t)0x01U)
};

// Arena chunk header.
struct arena_chunk_t {
  // Arena that owns the chunk.
  arena_t* mArena;

  // Linkage for the arena's tree of dirty chunks.
  RedBlackTreeNode<arena_chunk_t> mLinkDirty;

#ifdef MALLOC_DOUBLE_PURGE
  // If we're double-purging, we maintain a linked list of chunks which
  // have pages which have been madvise(MADV_FREE)'d but not explicitly
  // purged.
  //
  // We're currently lazy and don't remove a chunk from this list when
  // all its madvised pages are recommitted.
  mozilla::DoublyLinkedListElement<arena_chunk_t> mChunksMavisedElim;
#endif

  // Number of dirty pages that may be purged, the header is never counted
  // here.
  uint16_t mNumDirty = 0;

  // This will point to the page index of the first run that may have dirty
  // pages.
  uint16_t mDirtyRunHint;

  bool mIsPurging = false;
  bool mDying = false;

  // Map of pages within chunk that keeps track of free/large/small.
  arena_chunk_map_t mPageMap[];  // Dynamically sized.

  explicit arena_chunk_t(arena_t* aArena);

  bool IsEmpty();
};

[[nodiscard]] bool pages_commit(void* aAddr, size_t aSize);

void pages_decommit(void* aAddr, size_t aSize);

void chunks_init();

void* chunk_alloc(size_t aSize, size_t aAlignment, bool aBase);

void chunk_dealloc(void* aChunk, size_t aSize, ChunkType aType);
#ifdef MOZ_DEBUG
void chunk_assert_zero(void* aPtr, size_t aSize);
#endif

extern mozilla::Atomic<size_t> gRecycledSize;

extern AddressRadixTree<(sizeof(void*) << 3) - LOG2(kChunkSize)> gChunkRTree;

#endif /* ! CHUNK_H */
