/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gc/BufferAllocator-inl.h"

#include "mozilla/PodOperations.h"
#include "mozilla/ScopeExit.h"

#ifdef XP_DARWIN
#  include <mach/mach_init.h>
#  include <mach/vm_map.h>
#endif

#include "gc/BufferAllocatorInternals.h"
#include "gc/GCInternals.h"
#include "gc/GCLock.h"
#include "gc/PublicIterators.h"
#include "gc/Zone.h"
#include "js/HeapAPI.h"
#include "util/Poison.h"

#include "gc/Heap-inl.h"
#include "gc/Marking-inl.h"

using namespace js;
using namespace js::gc;

namespace js::gc {

BufferAllocator::AutoLock::AutoLock(GCRuntime* gc)
    : LockGuard(gc->bufferAllocatorLock) {}

BufferAllocator::AutoLock::AutoLock(BufferAllocator* allocator)
    : LockGuard(allocator->lock()) {}

static void CheckHighBitsOfPointer(void* ptr) {
#ifdef JS_64BIT
  // We require bit 48 and higher be clear.
  MOZ_DIAGNOSTIC_ASSERT((uintptr_t(ptr) >> 47) == 0);
#endif
}

BufferAllocator::FreeLists::FreeLists(FreeLists&& other) {
  MOZ_ASSERT(this != &other);
  assertEmpty();
  std::swap(lists, other.lists);
  std::swap(available, other.available);
  other.assertEmpty();
}

BufferAllocator::FreeLists& BufferAllocator::FreeLists::operator=(
    FreeLists&& other) {
  MOZ_ASSERT(this != &other);
  assertEmpty();
  std::swap(lists, other.lists);
  std::swap(available, other.available);
  other.assertEmpty();
  return *this;
}

BufferAllocator::FreeLists::FreeListIter
BufferAllocator::FreeLists::freeListIter() {
  return FreeListIter(*this);
}

BufferAllocator::FreeLists::FreeRegionIter
BufferAllocator::FreeLists::freeRegionIter() {
  return FreeRegionIter(*this);
}

bool BufferAllocator::FreeLists::hasSizeClass(size_t sizeClass) const {
  MOZ_ASSERT(sizeClass <= MaxMediumAllocClass);
  return available[sizeClass];
}

size_t BufferAllocator::FreeLists::getFirstAvailableSizeClass(
    size_t minSizeClass, size_t maxSizeClass) const {
  MOZ_ASSERT(maxSizeClass <= MaxMediumAllocClass);

  size_t result = available.FindNext(minSizeClass);
  MOZ_ASSERT(result >= minSizeClass);
  MOZ_ASSERT_IF(result != SIZE_MAX, !lists[result].isEmpty());

  if (result > maxSizeClass) {
    return SIZE_MAX;
  }

  return result;
}

size_t BufferAllocator::FreeLists::getLastAvailableSizeClass(
    size_t minSizeClass, size_t maxSizeClass) const {
  MOZ_ASSERT(maxSizeClass <= MaxMediumAllocClass);

  size_t result = available.FindPrev(maxSizeClass);
  MOZ_ASSERT(result <= maxSizeClass || result == SIZE_MAX);
  MOZ_ASSERT_IF(result != SIZE_MAX, !lists[result].isEmpty());

  if (result < minSizeClass) {
    return SIZE_MAX;
  }

  return result;
}

BufferAllocator::FreeRegion* BufferAllocator::FreeLists::getFirstRegion(
    size_t sizeClass) {
  MOZ_ASSERT(!lists[sizeClass].isEmpty());
  return lists[sizeClass].getFirst();
}

void BufferAllocator::FreeLists::pushFront(size_t sizeClass,
                                           FreeRegion* region) {
  MOZ_ASSERT(sizeClass < AllocSizeClasses);
  lists[sizeClass].pushFront(region);
  available[sizeClass] = true;
}

void BufferAllocator::FreeLists::pushBack(size_t sizeClass,
                                          FreeRegion* region) {
  MOZ_ASSERT(sizeClass < AllocSizeClasses);
  lists[sizeClass].pushBack(region);
  available[sizeClass] = true;
}

void BufferAllocator::FreeLists::append(FreeLists&& other) {
  for (size_t i = 0; i < AllocSizeClasses; i++) {
    if (!other.lists[i].isEmpty()) {
      lists[i].append(std::move(other.lists[i]));
      available[i] = true;
    }
  }
  other.available.ResetAll();
  other.assertEmpty();
}

void BufferAllocator::FreeLists::prepend(FreeLists&& other) {
  for (size_t i = 0; i < AllocSizeClasses; i++) {
    if (!other.lists[i].isEmpty()) {
      lists[i].prepend(std::move(other.lists[i]));
      available[i] = true;
    }
  }
  other.available.ResetAll();
  other.assertEmpty();
}

void BufferAllocator::FreeLists::remove(size_t sizeClass, FreeRegion* region) {
  MOZ_ASSERT(sizeClass < AllocSizeClasses);
  lists[sizeClass].remove(region);
  available[sizeClass] = !lists[sizeClass].isEmpty();
}

void BufferAllocator::FreeLists::clear() {
  for (auto freeList = freeListIter(); !freeList.done(); freeList.next()) {
    new (&freeList.get()) FreeList;  // clear() is less efficient.
  }
  available.ResetAll();
}

template <typename Func>
void BufferAllocator::FreeLists::forEachRegion(Func&& func) {
  for (size_t i = 0; i <= MaxMediumAllocClass; i++) {
    FreeList& freeList = lists[i];
    FreeRegion* region = freeList.getFirst();
    while (region) {
      FreeRegion* next = region->getNext();
      func(freeList, i, region);
      region = next;
    }
    available[i] = !freeList.isEmpty();
  }
}

inline void BufferAllocator::FreeLists::assertEmpty() const {
#ifdef DEBUG
  for (size_t i = 0; i < AllocSizeClasses; i++) {
    MOZ_ASSERT(lists[i].isEmpty());
  }
  MOZ_ASSERT(available.IsEmpty());
#endif
}

inline void BufferAllocator::FreeLists::assertContains(
    size_t sizeClass, FreeRegion* region) const {
#ifdef DEBUG
  MOZ_ASSERT(available[sizeClass]);
  MOZ_ASSERT(lists[sizeClass].contains(region));
#endif
}

inline void BufferAllocator::FreeLists::checkAvailable() const {
#ifdef DEBUG
  for (size_t i = 0; i < AllocSizeClasses; i++) {
    MOZ_ASSERT(available[i] == !lists[i].isEmpty());
  }
#endif
}

BufferAllocator::ChunkLists::ChunkListIter
BufferAllocator::ChunkLists::chunkListIter() {
  return ChunkListIter(*this);
}

BufferAllocator::ChunkLists::ChunkIter
BufferAllocator::ChunkLists::chunkIter() {
  return ChunkIter(*this);
}

size_t BufferAllocator::ChunkLists::getFirstAvailableSizeClass(
    size_t minSizeClass, size_t maxSizeClass) const {
  MOZ_ASSERT(maxSizeClass <= MaxMediumAllocClass);

  size_t result = available.FindNext(minSizeClass);
  MOZ_ASSERT(result >= minSizeClass);
  MOZ_ASSERT_IF(result != SIZE_MAX, !lists[result].isEmpty());

  if (result > maxSizeClass) {
    return SIZE_MAX;
  }

  return result;
}

BufferChunk* BufferAllocator::ChunkLists::popFirstChunk(size_t sizeClass) {
  MOZ_ASSERT(sizeClass < AllocSizeClasses);
  MOZ_ASSERT(!lists[sizeClass].isEmpty());
  BufferChunk* chunk = lists[sizeClass].popFirst();
  if (lists[sizeClass].isEmpty()) {
    available[sizeClass] = false;
  }
  return chunk;
}

void BufferAllocator::ChunkLists::remove(size_t sizeClass, BufferChunk* chunk) {
  MOZ_ASSERT(sizeClass <= AllocSizeClasses);
  lists[sizeClass].remove(chunk);
  available[sizeClass] = !lists[sizeClass].isEmpty();
}

void BufferAllocator::ChunkLists::pushFront(size_t sizeClass,
                                            BufferChunk* chunk) {
  MOZ_ASSERT(sizeClass <= AllocSizeClasses);
  lists[sizeClass].pushFront(chunk);
  available[sizeClass] = true;
}

void BufferAllocator::ChunkLists::pushBack(BufferChunk* chunk) {
  MOZ_ASSERT(chunk->ownsFreeLists);
  pushBack(chunk->sizeClassForAvailableLists(), chunk);
}

void BufferAllocator::ChunkLists::pushBack(size_t sizeClass,
                                           BufferChunk* chunk) {
  MOZ_ASSERT(sizeClass <= AllocSizeClasses);
  MOZ_ASSERT(sizeClass == chunk->sizeClassForAvailableLists());
  lists[sizeClass].pushBack(chunk);
  available[sizeClass] = true;
}

BufferAllocator::BufferChunkList
BufferAllocator::ChunkLists::extractAllChunks() {
  BufferChunkList result;
  for (auto list = chunkListIter(); !list.done(); list.next()) {
    result.append(std::move(list.get()));
  }
  available.ResetAll();
  return result;
}

inline bool BufferAllocator::ChunkLists::isEmpty() const {
  checkAvailable();
  return available.IsEmpty();
}

inline void BufferAllocator::ChunkLists::checkAvailable() const {
#ifdef DEBUG
  for (size_t i = 0; i < AllocSizeClasses; i++) {
    MOZ_ASSERT(available[i] == !lists[i].isEmpty());
  }
#endif
}

}  // namespace js::gc

MOZ_ALWAYS_INLINE void PoisonAlloc(void* alloc, uint8_t value, size_t bytes,
                                   MemCheckKind kind) {
#ifndef EARLY_BETA_OR_EARLIER
  // Limit poisoning in release builds.
  bytes = std::min(bytes, size_t(256));
#endif
  AlwaysPoison(alloc, value, bytes, kind);
}

template <typename D, size_t S, size_t G>
void AllocSpace<D, S, G>::setAllocated(void* alloc, size_t bytes,
                                       bool allocated) {
  size_t startBit = ptrToIndex(alloc);
  MOZ_ASSERT(bytes % GranularityBytes == 0);
  size_t endBit = startBit + bytes / GranularityBytes;
  MOZ_ASSERT(endBit <= MaxAllocCount);
  MOZ_ASSERT(allocStartBitmap.ref()[startBit] != allocated);
  MOZ_ASSERT_IF(endBit != MaxAllocCount, allocStartBitmap.ref()[startBit] ==
                                             allocEndBitmap.ref()[endBit]);
  MOZ_ASSERT_IF(startBit + 1 < MaxAllocCount,
                allocStartBitmap.ref().FindNext(startBit + 1) >= endBit);
  MOZ_ASSERT(findEndBit(startBit) >= endBit);

  allocStartBitmap.ref()[startBit] = allocated;
  if (endBit != MaxAllocCount) {
    allocEndBitmap.ref()[endBit] = allocated;
  }
}

template <typename D, size_t S, size_t G>
void AllocSpace<D, S, G>::updateEndOffset(void* alloc, size_t oldBytes,
                                          size_t newBytes) {
  MOZ_ASSERT(isAllocated(alloc));
  MOZ_ASSERT(oldBytes % GranularityBytes == 0);
  MOZ_ASSERT(newBytes % GranularityBytes == 0);

  size_t startBit = ptrToIndex(alloc);
  size_t oldEndBit = startBit + oldBytes / GranularityBytes;
  MOZ_ASSERT(oldEndBit <= MaxAllocCount);
  if (oldEndBit != MaxAllocCount) {
    MOZ_ASSERT(allocEndBitmap.ref()[oldEndBit]);
    allocEndBitmap.ref()[oldEndBit] = false;
  }

  size_t newEndBit = startBit + newBytes / GranularityBytes;
  MOZ_ASSERT(newEndBit <= MaxAllocCount);
  MOZ_ASSERT_IF(startBit + 1 < MaxAllocCount,
                allocStartBitmap.ref().FindNext(startBit + 1) >= newEndBit);
  MOZ_ASSERT(findEndBit(startBit) >= newEndBit);
  if (newEndBit != MaxAllocCount) {
    allocEndBitmap.ref()[newEndBit] = true;
  }
}

template <typename D, size_t S, size_t G>
size_t AllocSpace<D, S, G>::allocBytes(const void* alloc) const {
  MOZ_ASSERT(isAllocated(alloc));

  size_t startBit = ptrToIndex(alloc);
  size_t endBit = findEndBit(startBit);
  MOZ_ASSERT(endBit > startBit);
  MOZ_ASSERT(endBit <= MaxAllocCount);

  return (endBit - startBit) * GranularityBytes;
}

template <typename D, size_t S, size_t G>
bool AllocSpace<D, S, G>::setMarked(void* alloc) {
  MOZ_ASSERT(isAllocated(alloc));
  size_t bit = ptrToIndex(alloc);

  // This is thread safe but can return false positives if another thread also
  // marked the same allocation at the same time;
  if (markBits.ref().getBit(bit)) {
    return false;
  }

  markBits.ref().setBit(bit, true);
  return true;
}

template <typename D, size_t S, size_t G>
size_t AllocSpace<D, S, G>::findNextAllocated(uintptr_t offset) const {
  size_t bit = offsetToIndex(offset);
  size_t next = allocStartBitmap.ref().FindNext(bit);
  if (next == SIZE_MAX) {
    return SizeBytes;
  }

  return next * GranularityBytes;
}

template <typename D, size_t S, size_t G>
size_t AllocSpace<D, S, G>::findPrevAllocated(uintptr_t offset) const {
  size_t bit = offsetToIndex(offset);
  size_t prev = allocStartBitmap.ref().FindPrev(bit);
  if (prev == SIZE_MAX) {
    return SizeBytes;
  }

  return prev * GranularityBytes;
}

template <typename D, size_t S, size_t G>
BufferAllocator::FreeRegion* AllocSpace<D, S, G>::findFollowingFreeRegion(
    uintptr_t startAddr) {
  // Find the free region that starts at |startAddr|, which is not allocated and
  // not at the end of the chunk. Always returns a region.

  uintptr_t offset = uintptr_t(startAddr) & AddressMask;
  MOZ_ASSERT(isValidOffset(offset));
  MOZ_ASSERT((offset % GranularityBytes) == 0);

  MOZ_ASSERT(!isAllocated(offset));  // Already marked as not allocated.
  offset = findNextAllocated(offset);
  MOZ_ASSERT(offset <= SizeBytes);

  auto* region = FreeRegion::fromEndAddr(startAddress() + offset);
  MOZ_ASSERT(region->startAddr == startAddr);

  return region;
}

template <typename D, size_t S, size_t G>
BufferAllocator::FreeRegion* AllocSpace<D, S, G>::findPrecedingFreeRegion(
    uintptr_t endAddr) {
  // Find the free region, if any, that ends at |endAddr|, which may be
  // allocated or at the start of the chunk.

  uintptr_t offset = uintptr_t(endAddr) & AddressMask;
  MOZ_ASSERT(isValidOffset(offset));
  MOZ_ASSERT((offset % GranularityBytes) == 0);

  if (offset == firstAllocOffset()) {
    return nullptr;  // Already at start of chunk.
  }

  MOZ_ASSERT(!isAllocated(offset));
  offset = findPrevAllocated(offset);

  if (offset != SizeBytes) {
    // Found a preceding allocation.
    const void* alloc = ptrFromOffset(offset);
    size_t bytes = allocBytes(alloc);
    MOZ_ASSERT(uintptr_t(alloc) + bytes <= endAddr);
    if (uintptr_t(alloc) + bytes == endAddr) {
      // No free space between preceding allocation and |endAddr|.
      return nullptr;
    }
  }

  auto* region = FreeRegion::fromEndAddr(endAddr);

#ifdef DEBUG
  region->check();
  if (offset != SizeBytes) {
    const void* alloc = ptrFromOffset(offset);
    size_t bytes = allocBytes(alloc);
    MOZ_ASSERT(region->startAddr == uintptr_t(alloc) + bytes);
  } else {
    MOZ_ASSERT(region->startAddr == startAddress() + firstAllocOffset());
  }
#endif

  return region;
}

BufferChunk::BufferChunk(Zone* zone)
    : ChunkBase(zone->runtimeFromMainThread(), ChunkKind::Buffers) {
#ifdef DEBUG
  this->zone = zone;
  MOZ_ASSERT(decommittedPages.ref().IsEmpty());
#endif
}

BufferChunk::~BufferChunk() {
#ifdef DEBUG
  MOZ_ASSERT(allocStartBitmap.ref().IsEmpty());
  MOZ_ASSERT(allocEndBitmap.ref().IsEmpty());
  MOZ_ASSERT(nurseryOwnedBitmap.ref().IsEmpty());
#endif
}

void BufferChunk::setSmallBufferRegion(void* alloc, bool smallAlloc) {
  MOZ_ASSERT(isAllocated(alloc));
  size_t bit = ptrToIndex<SmallRegionSize, SmallRegionSize>(alloc);
  smallRegionBitmap.ref().setBit(bit, smallAlloc);
}

bool BufferChunk::isSmallBufferRegion(const void* alloc) const {
  // Allow any valid small alloc pointer within the region.
  size_t bit = ptrToIndex<SmallRegionSize, SmallAllocGranularity>(alloc);
  return smallRegionBitmap.ref().getBit(bit);
}

size_t BufferChunk::sizeClassForAvailableLists() const {
  MOZ_ASSERT(ownsFreeLists);

  // To quickly find an available chunk we bin them by the size of their largest
  // free region. This allows us to select a chunk we know will be able to
  // satisfy a request.
  //
  // This prioritises allocating into chunks with large free regions first. It
  // might be better for memory use to allocate into chunks with less free space
  // first instead.
  size_t sizeClass =
      freeLists.ref().getLastAvailableSizeClass(0, MaxMediumAllocClass);

  // Use a special size class for completely full chunks.
  if (sizeClass == SIZE_MAX) {
    return BufferAllocator::FullChunkSizeClass;
  }

  return sizeClass;
}

void SmallBufferRegion::setHasNurseryOwnedAllocs(bool value) {
  hasNurseryOwnedAllocs_ = value;
}
bool SmallBufferRegion::hasNurseryOwnedAllocs() const {
  return hasNurseryOwnedAllocs_.ref();
}

BufferAllocator::BufferAllocator(Zone* zone)
    : zone(zone),
      sweptMixedChunks(lock()),
      sweptTenuredChunks(lock()),
      sweptLargeTenuredAllocs(lock()),
      minorState(State::NotCollecting),
      majorState(State::NotCollecting),
      minorSweepingFinished(lock()),
      majorSweepingFinished(lock()) {}

BufferAllocator::~BufferAllocator() {
#ifdef DEBUG
  checkGCStateNotInUse();
  MOZ_ASSERT(mixedChunks.ref().isEmpty());
  MOZ_ASSERT(tenuredChunks.ref().isEmpty());
  freeLists.ref().assertEmpty();
  MOZ_ASSERT(availableMixedChunks.ref().isEmpty());
  MOZ_ASSERT(availableTenuredChunks.ref().isEmpty());
  MOZ_ASSERT(largeNurseryAllocs.ref().isEmpty());
  MOZ_ASSERT(largeTenuredAllocs.ref().isEmpty());
#endif
}

bool BufferAllocator::isEmpty() const {
  MOZ_ASSERT(!zone->wasGCStarted() || zone->isGCFinished());
  MOZ_ASSERT(minorState == State::NotCollecting);
  MOZ_ASSERT(majorState == State::NotCollecting);
  return mixedChunks.ref().isEmpty() && availableMixedChunks.ref().isEmpty() &&
         tenuredChunks.ref().isEmpty() &&
         availableTenuredChunks.ref().isEmpty() &&
         largeNurseryAllocs.ref().isEmpty() &&
         largeTenuredAllocs.ref().isEmpty();
}

Mutex& BufferAllocator::lock() const {
  return zone->runtimeFromAnyThread()->gc.bufferAllocatorLock;
}

void* BufferAllocator::alloc(size_t bytes, bool nurseryOwned) {
  MOZ_ASSERT_IF(zone->isGCMarkingOrSweeping(), majorState == State::Marking);

  if (IsLargeAllocSize(bytes)) {
    return allocLarge(bytes, nurseryOwned, false);
  }

  if (IsSmallAllocSize(bytes)) {
    return allocSmall(bytes, nurseryOwned, false);
  }

  return allocMedium(bytes, nurseryOwned, false);
}

void* BufferAllocator::allocInGC(size_t bytes, bool nurseryOwned) {
  // Currently this is used during tenuring only.
  MOZ_ASSERT(minorState == State::Marking);

  MOZ_ASSERT_IF(zone->isGCMarkingOrSweeping(), majorState == State::Marking);

  void* result;
  if (IsLargeAllocSize(bytes)) {
    result = allocLarge(bytes, nurseryOwned, true);
  } else if (IsSmallAllocSize(bytes)) {
    result = allocSmall(bytes, nurseryOwned, true);
  } else {
    result = allocMedium(bytes, nurseryOwned, true);
  }

  if (!result) {
    return nullptr;
  }

  // Barrier to mark nursery-owned allocations that happen during collection. We
  // don't need to do this for tenured-owned allocations because we don't sweep
  // tenured-owned allocations that happened after the start of a major
  // collection.
  if (nurseryOwned) {
    markNurseryOwnedAlloc(result, false);
  }

  return result;
}

#ifdef DEBUG

inline Zone* LargeBuffer::zone() {
  Zone* zone = zoneFromAnyThread();
  MOZ_ASSERT(CurrentThreadCanAccessZone(zone));
  return zone;
}

inline Zone* LargeBuffer::zoneFromAnyThread() {
  return BufferChunk::from(this)->zone;
}

#endif

#ifdef XP_DARWIN
static inline void VirtualCopyPages(void* dst, const void* src, size_t bytes) {
  MOZ_ASSERT((uintptr_t(dst) & PageMask) == 0);
  MOZ_ASSERT((uintptr_t(src) & PageMask) == 0);
  MOZ_ASSERT(bytes >= ChunkSize);

  kern_return_t r = vm_copy(mach_task_self(), vm_address_t(src),
                            vm_size_t(bytes), vm_address_t(dst));
  if (r != KERN_SUCCESS) {
    MOZ_CRASH("vm_copy() failed");
  }
}
#endif

void* BufferAllocator::realloc(void* alloc, size_t bytes, bool nurseryOwned) {
  // Reallocate a buffer. This has the same semantics as standard libarary
  // realloc: if |ptr| is null it creates a new allocation, and if it fails it
  // returns |nullptr| and the original |ptr| is still valid.

  if (!alloc) {
    return this->alloc(bytes, nurseryOwned);
  }

  MOZ_ASSERT(isNurseryOwned(alloc) == nurseryOwned);
  MOZ_ASSERT_IF(zone->isGCMarkingOrSweeping(), majorState == State::Marking);

  bytes = GetGoodAllocSize(bytes);

  size_t currentBytes;
  if (IsLargeAlloc(alloc)) {
    LargeBuffer* buffer = lookupLargeBuffer(alloc);
    currentBytes = buffer->allocBytes();

    // We can shrink large allocations (on some platforms).
    if (bytes < buffer->allocBytes() && IsLargeAllocSize(bytes)) {
      if (shrinkLarge(buffer, bytes)) {
        return alloc;
      }
    }
  } else if (IsMediumAlloc(alloc)) {
    BufferChunk* chunk = BufferChunk::from(alloc);
    MOZ_ASSERT(!chunk->isSmallBufferRegion(alloc));

    currentBytes = chunk->allocBytes(alloc);

    // We can grow or shrink medium allocations.
    if (bytes < currentBytes && !IsSmallAllocSize(bytes)) {
      if (shrinkMedium(alloc, bytes)) {
        return alloc;
      }
    }

    if (bytes > currentBytes && !IsLargeAllocSize(bytes)) {
      if (growMedium(alloc, bytes)) {
        return alloc;
      }
    }
  } else {
    // TODO: Grow and shrink small allocations.
    auto* region = SmallBufferRegion::from(alloc);
    currentBytes = region->allocBytes(alloc);
  }

  if (bytes == currentBytes) {
    return alloc;
  }

  void* newAlloc = this->alloc(bytes, nurseryOwned);
  if (!newAlloc) {
    return nullptr;
  }

  auto freeGuard = mozilla::MakeScopeExit([&]() { free(alloc); });

  size_t bytesToCopy = std::min(bytes, currentBytes);

#ifdef XP_DARWIN
  if (bytesToCopy >= ChunkSize) {
    MOZ_ASSERT(IsLargeAlloc(alloc));
    MOZ_ASSERT(IsLargeAlloc(newAlloc));
    VirtualCopyPages(newAlloc, alloc, bytesToCopy);
    return newAlloc;
  }
#endif

  memcpy(newAlloc, alloc, bytesToCopy);
  return newAlloc;
}

void BufferAllocator::free(void* alloc) {
  MOZ_ASSERT(alloc);

  if (IsLargeAlloc(alloc)) {
    freeLarge(alloc);
    return;
  }

  if (IsMediumAlloc(alloc)) {
    freeMedium(alloc);
    return;
  }

  // Can't free small allocations.
}

/* static */
bool BufferAllocator::IsBufferAlloc(void* alloc) {
  // Precondition: |alloc| is a pointer to a buffer allocation, a GC thing or a
  // direct nursery allocation returned by Nursery::allocateBuffer.

  if (IsLargeAlloc(alloc)) {
    return true;
  }

  ChunkBase* chunk = detail::GetGCAddressChunkBase(alloc);
  return chunk->getKind() == ChunkKind::Buffers;
}

size_t BufferAllocator::getAllocSize(void* alloc) {
  if (IsLargeAlloc(alloc)) {
    LargeBuffer* buffer = lookupLargeBuffer(alloc);
    return buffer->allocBytes();
  }

  if (IsSmallAlloc(alloc)) {
    auto* region = SmallBufferRegion::from(alloc);
    return region->allocBytes(alloc);
  }

  MOZ_ASSERT(IsMediumAlloc(alloc));
  BufferChunk* chunk = BufferChunk::from(alloc);
  return chunk->allocBytes(alloc);
}

bool BufferAllocator::isNurseryOwned(void* alloc) {
  if (IsLargeAlloc(alloc)) {
    LargeBuffer* buffer = lookupLargeBuffer(alloc);
    return buffer->isNurseryOwned;
  }

  if (IsSmallAlloc(alloc)) {
    auto* region = SmallBufferRegion::from(alloc);
    return region->isNurseryOwned(alloc);
  }

  BufferChunk* chunk = BufferChunk::from(alloc);
  return chunk->isNurseryOwned(alloc);
}

void BufferAllocator::markNurseryOwnedAlloc(void* alloc, bool ownerWasTenured) {
  MOZ_ASSERT(alloc);
  MOZ_ASSERT(isNurseryOwned(alloc));
  MOZ_ASSERT(minorState == State::Marking);

  if (IsLargeAlloc(alloc)) {
    LargeBuffer* buffer = lookupLargeBuffer(alloc);
    MOZ_ASSERT(buffer->zone() == zone);
    markLargeNurseryOwnedBuffer(buffer, ownerWasTenured);
    return;
  }

  if (IsSmallAlloc(alloc)) {
    markSmallNurseryOwnedBuffer(alloc, ownerWasTenured);
    return;
  }

  MOZ_ASSERT(IsMediumAlloc(alloc));
  markMediumNurseryOwnedBuffer(alloc, ownerWasTenured);
}

void BufferAllocator::markSmallNurseryOwnedBuffer(void* alloc,
                                                  bool ownerWasTenured) {
#ifdef DEBUG
  BufferChunk* chunk = BufferChunk::from(alloc);
  MOZ_ASSERT(chunk->zone == zone);
  MOZ_ASSERT(chunk->hasNurseryOwnedAllocs);
#endif

  auto* region = SmallBufferRegion::from(alloc);
  MOZ_ASSERT(region->hasNurseryOwnedAllocs());
  MOZ_ASSERT(region->isNurseryOwned(alloc));

  if (ownerWasTenured) {
    region->setNurseryOwned(alloc, false);
    // If all nursery owned allocations in the region were tenured then
    // chunk->isNurseryOwned(region) will now be stale. It will be updated when
    // the region is swept.
    return;
  }

  region->setMarked(alloc);
}

void BufferAllocator::markMediumNurseryOwnedBuffer(void* alloc,
                                                   bool ownerWasTenured) {
  BufferChunk* chunk = BufferChunk::from(alloc);
  MOZ_ASSERT(chunk->zone == zone);
  MOZ_ASSERT(chunk->hasNurseryOwnedAllocs);
  MOZ_ASSERT(chunk->isAllocated(alloc));
  MOZ_ASSERT(chunk->isNurseryOwned(alloc));

  if (ownerWasTenured) {
    // Change the allocation to a tenured owned one. This prevents sweeping in a
    // minor collection.
    chunk->setNurseryOwned(alloc, false);
    size_t size = chunk->allocBytes(alloc);
    updateHeapSize(size, false, false);
    return;
  }

  chunk->setMarked(alloc);
}

void BufferAllocator::markLargeNurseryOwnedBuffer(LargeBuffer* buffer,
                                                  bool ownerWasTenured) {
  MOZ_ASSERT(buffer->isNurseryOwned);

  // The buffer metadata is held in a small buffer.
  auto* region = SmallBufferRegion::from(buffer);
  if (region->isNurseryOwned(buffer)) {
    markSmallNurseryOwnedBuffer(buffer, ownerWasTenured);
  }

  largeNurseryAllocsToSweep.ref().remove(buffer);

  if (ownerWasTenured) {
    buffer->isNurseryOwned = false;
    buffer->allocatedDuringCollection = majorState != State::NotCollecting;
    largeTenuredAllocs.ref().pushBack(buffer);
    size_t usableSize = buffer->allocBytes();
    updateHeapSize(usableSize, false, false);
    return;
  }

  largeNurseryAllocs.ref().pushBack(buffer);
}

bool BufferAllocator::isMarkedBlack(void* alloc) {
  if (IsLargeAlloc(alloc)) {
    // The buffer metadata is held in a small buffer.
    alloc = lookupLargeBuffer(alloc);
  } else if (!IsSmallAlloc(alloc)) {
    MOZ_ASSERT(IsMediumAlloc(alloc));
    BufferChunk* chunk = BufferChunk::from(alloc);
    return chunk->isMarked(alloc);
  }

  auto* region = SmallBufferRegion::from(alloc);
  return region->isMarked(alloc);
}

void BufferAllocator::traceEdge(JSTracer* trc, Cell* owner, void** bufferp,
                                const char* name) {
  // Buffers are conceptually part of the owning cell and are not reported to
  // the tracer.

  // TODO: This should be unified with the rest of the tracing system.

  MOZ_ASSERT(owner);
  MOZ_ASSERT(bufferp);

  void* buffer = *bufferp;
  MOZ_ASSERT(buffer);

  if (!IsLargeAlloc(buffer) &&
      js::gc::detail::GetGCAddressChunkBase(buffer)->isNurseryChunk()) {
    // JSObject slots and elements can be allocated in the nursery and this is
    // handled separately.
    return;
  }

  MOZ_ASSERT(IsBufferAlloc(buffer));

  if (IsLargeAlloc(buffer)) {
    traceLargeAlloc(trc, owner, bufferp, name);
    return;
  }

  if (IsSmallAlloc(buffer)) {
    traceSmallAlloc(trc, owner, bufferp, name);
    return;
  }

  traceMediumAlloc(trc, owner, bufferp, name);
}

void BufferAllocator::traceSmallAlloc(JSTracer* trc, Cell* owner, void** allocp,
                                      const char* name) {
  void* alloc = *allocp;
  auto* region = SmallBufferRegion::from(alloc);

  if (trc->isTenuringTracer()) {
    if (region->isNurseryOwned(alloc)) {
      markSmallNurseryOwnedBuffer(alloc, owner->isTenured());
    }
    return;
  }

  if (trc->isMarkingTracer()) {
    if (!region->isNurseryOwned(alloc)) {
      markSmallTenuredAlloc(alloc);
    }
    return;
  }
}

void BufferAllocator::traceMediumAlloc(JSTracer* trc, Cell* owner,
                                       void** allocp, const char* name) {
  void* alloc = *allocp;
  BufferChunk* chunk = BufferChunk::from(alloc);

  if (trc->isTenuringTracer()) {
    if (chunk->isNurseryOwned(alloc)) {
      markMediumNurseryOwnedBuffer(alloc, owner->isTenured());
    }
    return;
  }

  if (trc->isMarkingTracer()) {
    if (!chunk->isNurseryOwned(alloc)) {
      markMediumTenuredAlloc(alloc);
    }
    return;
  }
}

void BufferAllocator::traceLargeAlloc(JSTracer* trc, Cell* owner, void** allocp,
                                      const char* name) {
  void* alloc = *allocp;
  LargeBuffer* buffer = lookupLargeBuffer(alloc);

  // Trace small buffer that holds large buffer metadata.
  traceSmallAlloc(trc, owner, reinterpret_cast<void**>(&buffer), "LargeBuffer");

  if (trc->isTenuringTracer()) {
    if (isNurseryOwned(alloc)) {
      markLargeNurseryOwnedBuffer(buffer, owner->isTenured());
    }
    return;
  }

  if (trc->isMarkingTracer()) {
    if (!isNurseryOwned(alloc)) {
      markLargeTenuredBuffer(buffer);
    }
    return;
  }
}

bool BufferAllocator::markTenuredAlloc(void* alloc) {
  MOZ_ASSERT(alloc);
  MOZ_ASSERT(!isNurseryOwned(alloc));

  if (IsLargeAlloc(alloc)) {
    LargeBuffer* buffer = lookupLargeBuffer(alloc);
    return markLargeTenuredBuffer(buffer);
  }

  if (IsSmallAlloc(alloc)) {
    return markSmallTenuredAlloc(alloc);
  }

  return markMediumTenuredAlloc(alloc);
}

bool BufferAllocator::markSmallTenuredAlloc(void* alloc) {
  auto* chunk = BufferChunk::from(alloc);
  if (chunk->allocatedDuringCollection) {
    // Will not be swept, already counted as marked.
    return false;
  }

  auto* region = SmallBufferRegion::from(alloc);
  MOZ_ASSERT(region->isAllocated(alloc));
  return region->setMarked(alloc);
}

bool BufferAllocator::markMediumTenuredAlloc(void* alloc) {
  BufferChunk* chunk = BufferChunk::from(alloc);
  MOZ_ASSERT(chunk->isAllocated(alloc));
  if (chunk->allocatedDuringCollection) {
    // Will not be swept, already counted as marked.
    return false;
  }

  return chunk->setMarked(alloc);
}

void BufferAllocator::startMinorCollection(MaybeLock& lock) {
  maybeMergeSweptData(lock);

#ifdef DEBUG
  MOZ_ASSERT(minorState == State::NotCollecting);
  if (majorState == State::NotCollecting) {
    GCRuntime* gc = &zone->runtimeFromMainThread()->gc;
    if (gc->hasZealMode(ZealMode::CheckHeapBeforeMinorGC)) {
      // This is too expensive to run on every minor GC.
      checkGCStateNotInUse(lock);
    }
  }
#endif

  // Large allocations that are marked when tracing the nursery will be moved
  // back to the main list.
  MOZ_ASSERT(largeNurseryAllocsToSweep.ref().isEmpty());
  std::swap(largeNurseryAllocs.ref(), largeNurseryAllocsToSweep.ref());

  minorState = State::Marking;
}

bool BufferAllocator::startMinorSweeping() {
  // Called during minor GC. Operates on the active allocs/chunks lists. The 'to
  // sweep' lists do not contain nursery owned allocations.

#ifdef DEBUG
  MOZ_ASSERT(minorState == State::Marking);
  {
    AutoLock lock(this);
    MOZ_ASSERT(!minorSweepingFinished);
    MOZ_ASSERT(sweptMixedChunks.ref().isEmpty());
  }
  for (LargeBuffer* buffer : largeNurseryAllocs.ref()) {
    MOZ_ASSERT(buffer->isNurseryOwned);
  }
  for (LargeBuffer* buffer : largeNurseryAllocsToSweep.ref()) {
    MOZ_ASSERT(buffer->isNurseryOwned);
  }
#endif

  // Check whether there are any medium chunks containing nursery owned
  // allocations that need to be swept.
  if (mixedChunks.ref().isEmpty() && availableMixedChunks.ref().isEmpty() &&
      largeNurseryAllocsToSweep.ref().isEmpty()) {
    // Nothing more to do. Don't transition to sweeping state.
    minorState = State::NotCollecting;
    return false;
  }

#ifdef DEBUG
  for (BufferChunk* chunk : mixedChunks.ref()) {
    MOZ_ASSERT(!chunk->ownsFreeLists);
    chunk->freeLists.ref().assertEmpty();
  }
#endif

  // Move free regions in |tenuredChunks| out of |freeLists| and into their
  // respective chunk header. Discard free regions in |mixedChunks| which will
  // be rebuilt by sweeping.
  //
  // This is done for |tenuredChunks| too in order to reduce the number of free
  // regions we need to process here on the next minor GC.
  //
  // Some possibilities to make this more efficient are:
  //  - have separate free lists for nursery/tenured chunks
  //  - keep free regions at different ends of the free list depending on chunk
  //    kind
  freeLists.ref().forEachRegion(
      [](FreeList& list, size_t sizeClass, FreeRegion* region) {
        BufferChunk* chunk = BufferChunk::from(region);
        if (!chunk->hasNurseryOwnedAllocs) {
          list.remove(region);
          chunk->freeLists.ref().pushBack(sizeClass, region);
        }
      });
  freeLists.ref().clear();

  // Set the flag to indicate all tenured chunks now own their free regions.
  for (BufferChunk* chunk : tenuredChunks.ref()) {
    MOZ_ASSERT(!chunk->hasNurseryOwnedAllocs);
    chunk->ownsFreeLists = true;
  }

  // Move all mixed chunks to the list of chunks to sweep.
  mixedChunksToSweep.ref() = std::move(mixedChunks.ref());
  mixedChunksToSweep.ref().append(
      availableMixedChunks.ref().extractAllChunks());

  // Move all tenured chunks to |availableTenuredChunks|.
  while (BufferChunk* chunk = tenuredChunks.ref().popFirst()) {
    availableTenuredChunks.ref().pushBack(chunk);
  }

  minorState = State::Sweeping;

  return true;
}

struct LargeAllocToFree {
  size_t bytes;
  LargeAllocToFree* next = nullptr;

  explicit LargeAllocToFree(size_t bytes) : bytes(bytes) {}
};

static void PushLargeAllocToFree(LargeAllocToFree** listHead,
                                 LargeBuffer* buffer) {
  auto* alloc = new (buffer->data()) LargeAllocToFree(buffer->bytes);
  alloc->next = *listHead;
  *listHead = alloc;
}

static void FreeLargeAllocs(LargeAllocToFree* listHead) {
  while (listHead) {
    LargeAllocToFree* alloc = listHead;
    LargeAllocToFree* next = alloc->next;
    UnmapPages(alloc, alloc->bytes);
    listHead = next;
  }
}

void BufferAllocator::sweepForMinorCollection() {
  // Called on a background thread.

  MOZ_ASSERT(minorState.refNoCheck() == State::Sweeping);
  {
    AutoLock lock(this);
    MOZ_ASSERT(sweptMixedChunks.ref().isEmpty());
  }

  // Bug 1961749: Freeing large buffers can be slow so it might be worth
  // splitting sweeping into two phases so that all zones get their medium
  // buffers swept and made available for allocation before any large buffers
  // are freed.

  // Freeing large buffers may be slow, so leave that till the end. However
  // large buffer metadata is stored in small buffers so form a list of large
  // buffers to free before sweeping small buffers.
  LargeAllocToFree* largeAllocsToFree = nullptr;
  while (!largeNurseryAllocsToSweep.ref().isEmpty()) {
    LargeBuffer* buffer = largeNurseryAllocsToSweep.ref().popFirst();
    PushLargeAllocToFree(&largeAllocsToFree, buffer);
    MaybeLock lock(std::in_place, this);
    unregisterLarge(buffer, true, lock);
  }

  while (!mixedChunksToSweep.ref().isEmpty()) {
    BufferChunk* chunk = mixedChunksToSweep.ref().popFirst();
    if (sweepChunk(chunk, SweepKind::Nursery, false)) {
      {
        AutoLock lock(this);
        sweptMixedChunks.ref().pushBack(chunk);
      }

      // Signal to the main thread that swept data is available by setting this
      // relaxed atomic flag.
      hasMinorSweepDataToMerge = true;
    }
  }

  // Unmap large buffers.
  FreeLargeAllocs(largeAllocsToFree);

  // Signal to main thread to update minorState.
  {
    AutoLock lock(this);
    MOZ_ASSERT(!minorSweepingFinished);
    minorSweepingFinished = true;
    hasMinorSweepDataToMerge = true;
  }
}

void BufferAllocator::startMajorCollection(MaybeLock& lock) {
  maybeMergeSweptData(lock);

#ifdef DEBUG
  MOZ_ASSERT(majorState == State::NotCollecting);
  checkGCStateNotInUse(lock);

  // Everything is tenured since we just evicted the nursery, or will be by the
  // time minor sweeping finishes.
  MOZ_ASSERT(mixedChunks.ref().isEmpty());
  MOZ_ASSERT(availableMixedChunks.ref().isEmpty());
  MOZ_ASSERT(largeNurseryAllocs.ref().isEmpty());
#endif

#ifdef DEBUG
  for (BufferChunk* chunk : tenuredChunks.ref()) {
    MOZ_ASSERT(!chunk->ownsFreeLists);
    chunk->freeLists.ref().assertEmpty();
  }
#endif

  largeTenuredAllocsToSweep.ref() = std::move(largeTenuredAllocs.ref());

  // Move free regions that need to be swept to the free lists in their
  // respective chunks.
  freeLists.ref().forEachRegion(
      [](FreeList& list, size_t sizeClass, FreeRegion* region) {
        BufferChunk* chunk = BufferChunk::from(region);
        MOZ_ASSERT(!chunk->hasNurseryOwnedAllocs);
        list.remove(region);
        chunk->freeLists.ref().pushBack(sizeClass, region);
      });

  for (BufferChunk* chunk : tenuredChunks.ref()) {
    MOZ_ASSERT(!chunk->hasNurseryOwnedAllocs);
    chunk->ownsFreeLists = true;
  }

  tenuredChunksToSweep.ref() = std::move(tenuredChunks.ref());
  tenuredChunksToSweep.ref().append(
      availableTenuredChunks.ref().extractAllChunks());

  if (minorState == State::Sweeping) {
    // Ensure swept nursery chunks are moved to the tenuredChunks lists in
    // mergeSweptData.
    majorStartedWhileMinorSweeping = true;
  }

#ifdef DEBUG
  MOZ_ASSERT(tenuredChunks.ref().isEmpty());
  MOZ_ASSERT(availableTenuredChunks.ref().isEmpty());
  freeLists.ref().assertEmpty();
  MOZ_ASSERT(largeTenuredAllocs.ref().isEmpty());
#endif

  majorState = State::Marking;
}

void BufferAllocator::startMajorSweeping(MaybeLock& lock) {
  // Called when a zone transitions from marking to sweeping.

#ifdef DEBUG
  MOZ_ASSERT(majorState == State::Marking);
  MOZ_ASSERT(zone->isGCFinished());
  MOZ_ASSERT(!majorSweepingFinished.refNoCheck());
#endif

  maybeMergeSweptData(lock);
  MOZ_ASSERT(!majorStartedWhileMinorSweeping);

  majorState = State::Sweeping;
}

void BufferAllocator::sweepForMajorCollection(bool shouldDecommit) {
  // Called on a background thread.

  MOZ_ASSERT(majorState.refNoCheck() == State::Sweeping);

  // Sweep large allocs first since they rely on the mark bits of their
  // corresponding LargeBuffer structures which are stored small buffers.
  //
  // It's tempting to try and optimize this by moving the allocations between
  // lists when they are marked, in the same way as for nursery sweeping. This
  // would require synchronizing the list modification when marking in parallel,
  // so is probably not worth it.
  LargeAllocList sweptLargeAllocs;
  LargeAllocToFree* largeAllocsToFree = nullptr;
  while (!largeTenuredAllocsToSweep.ref().isEmpty()) {
    LargeBuffer* buffer = largeTenuredAllocsToSweep.ref().popFirst();
    if (isLargeTenuredMarked(buffer)) {
      sweptLargeAllocs.pushBack(buffer);
    } else {
      PushLargeAllocToFree(&largeAllocsToFree, buffer);
      MaybeLock lock(std::in_place, this);
      unregisterLarge(buffer, true, lock);
    }
  }

  while (!tenuredChunksToSweep.ref().isEmpty()) {
    BufferChunk* chunk = tenuredChunksToSweep.ref().popFirst();
    if (sweepChunk(chunk, SweepKind::Tenured, shouldDecommit)) {
      {
        AutoLock lock(this);
        sweptTenuredChunks.ref().pushBack(chunk);
      }

      // Signal to the main thread that swept data is available by setting this
      // relaxed atomic flag.
      hasMinorSweepDataToMerge = true;
    }
  }

  // Unmap large buffers.
  //
  // Bug 1961749: This could possibly run after signalling sweeping is finished
  // or concurrently with other sweeping.
  FreeLargeAllocs(largeAllocsToFree);

  AutoLock lock(this);
  sweptLargeTenuredAllocs.ref() = std::move(sweptLargeAllocs);

  // Signal to main thread to update majorState.
  MOZ_ASSERT(!majorSweepingFinished);
  majorSweepingFinished = true;
}

void BufferAllocator::finishMajorCollection(const AutoLock& lock) {
  // This can be called in any state:
  //
  //  - NotCollecting: after major sweeping has finished and the state has been
  //                   reset to NotCollecting in mergeSweptData.
  //
  //  - Marking:       if collection was aborted and startMajorSweeping was not
  //                   called.
  //
  //  - Sweeping:      if sweeping has finished and mergeSweptData has not been
  //                   called yet.

  MOZ_ASSERT_IF(majorState == State::Sweeping, majorSweepingFinished);

  if (minorState == State::Sweeping || majorState == State::Sweeping) {
    mergeSweptData(lock);
  }

  if (majorState == State::Marking) {
    abortMajorSweeping(lock);
  }

#ifdef DEBUG
  checkGCStateNotInUse(lock);
#endif
}

void BufferAllocator::abortMajorSweeping(const AutoLock& lock) {
  // We have aborted collection without sweeping this zone. Restore or rebuild
  // the original state.

#ifdef DEBUG
  MOZ_ASSERT(majorState == State::Marking);
  MOZ_ASSERT(sweptTenuredChunks.ref().isEmpty());
  for (auto chunk = availableTenuredChunks.ref().chunkIter(); !chunk.done();
       chunk.next()) {
    MOZ_ASSERT(chunk->allocatedDuringCollection);
  }
#endif

  clearAllocatedDuringCollectionState(lock);

  for (BufferChunk* chunk : tenuredChunksToSweep.ref()) {
    MOZ_ASSERT(chunk->ownsFreeLists);

    // Clear mark bits for chunks we didn't end up sweeping.
    clearChunkMarkBits(chunk);
  }

  while (BufferChunk* chunk = tenuredChunksToSweep.ref().popFirst()) {
    availableTenuredChunks.ref().pushBack(chunk);
  }

  largeTenuredAllocs.ref().prepend(std::move(largeTenuredAllocsToSweep.ref()));

  majorState = State::NotCollecting;
}

void BufferAllocator::clearAllocatedDuringCollectionState(
    const AutoLock& lock) {
#ifdef DEBUG
  // This flag is not set for large nursery-owned allocations.
  for (LargeBuffer* buffer : largeNurseryAllocs.ref()) {
    MOZ_ASSERT(!buffer->allocatedDuringCollection);
  }
#endif

  ClearAllocatedDuringCollection(mixedChunks.ref());
  ClearAllocatedDuringCollection(availableMixedChunks.ref());
  ClearAllocatedDuringCollection(tenuredChunks.ref());
  ClearAllocatedDuringCollection(availableTenuredChunks.ref());
  ClearAllocatedDuringCollection(largeTenuredAllocs.ref());
}

/* static */
void BufferAllocator::ClearAllocatedDuringCollection(ChunkLists& chunks) {
  for (auto chunk = chunks.chunkIter(); !chunk.done(); chunk.next()) {
    chunk->allocatedDuringCollection = false;
  }
}
/* static */
void BufferAllocator::ClearAllocatedDuringCollection(BufferChunkList& list) {
  for (auto* chunk : list) {
    chunk->allocatedDuringCollection = false;
  }
}
/* static */
void BufferAllocator::ClearAllocatedDuringCollection(LargeAllocList& list) {
  for (auto* element : list) {
    element->allocatedDuringCollection = false;
  }
}

void BufferAllocator::maybeMergeSweptData() {
  if (minorState == State::Sweeping || majorState == State::Sweeping) {
    mergeSweptData();
  }
}

void BufferAllocator::mergeSweptData() {
  AutoLock lock(this);
  mergeSweptData(lock);
}

void BufferAllocator::maybeMergeSweptData(MaybeLock& lock) {
  if (minorState == State::Sweeping || majorState == State::Sweeping) {
    if (lock.isNothing()) {
      lock.emplace(this);
    }
    mergeSweptData(lock.ref());
  }
}

void BufferAllocator::mergeSweptData(const AutoLock& lock) {
  MOZ_ASSERT(minorState == State::Sweeping || majorState == State::Sweeping);

  if (majorSweepingFinished) {
    clearAllocatedDuringCollectionState(lock);

    if (minorState == State::Sweeping) {
      majorFinishedWhileMinorSweeping = true;
    }
  }

  // Merge swept chunks that previously contained nursery owned allocations. If
  // semispace nursery collection is in use then these chunks may contain both
  // nursery and tenured-owned allocations, otherwise all allocations will be
  // tenured-owned.
  while (!sweptMixedChunks.ref().isEmpty()) {
    BufferChunk* chunk = sweptMixedChunks.ref().popLast();
    MOZ_ASSERT(chunk->ownsFreeLists);
    MOZ_ASSERT(chunk->hasNurseryOwnedAllocs);
    chunk->hasNurseryOwnedAllocs = chunk->hasNurseryOwnedAllocsAfterSweep;

    MOZ_ASSERT_IF(
        majorState == State::NotCollecting && !majorFinishedWhileMinorSweeping,
        !chunk->allocatedDuringCollection);
    if (majorFinishedWhileMinorSweeping) {
      chunk->allocatedDuringCollection = false;
    }

    size_t sizeClass = chunk->sizeClassForAvailableLists();
    if (chunk->hasNurseryOwnedAllocs) {
      availableMixedChunks.ref().pushFront(sizeClass, chunk);
    } else if (majorStartedWhileMinorSweeping) {
      tenuredChunksToSweep.ref().pushFront(chunk);
    } else {
      availableTenuredChunks.ref().pushFront(sizeClass, chunk);
    }
  }

  // Merge swept chunks that did not contain nursery owned allocations.
#ifdef DEBUG
  for (BufferChunk* chunk : sweptTenuredChunks.ref()) {
    MOZ_ASSERT(!chunk->hasNurseryOwnedAllocs);
    MOZ_ASSERT(!chunk->hasNurseryOwnedAllocsAfterSweep);
    MOZ_ASSERT(!chunk->allocatedDuringCollection);
  }
#endif
  while (BufferChunk* chunk = sweptTenuredChunks.ref().popFirst()) {
    size_t sizeClass = chunk->sizeClassForAvailableLists();
    availableTenuredChunks.ref().pushFront(sizeClass, chunk);
  }

  largeTenuredAllocs.ref().prepend(std::move(sweptLargeTenuredAllocs.ref()));

  hasMinorSweepDataToMerge = false;

  if (minorSweepingFinished) {
    MOZ_ASSERT(minorState == State::Sweeping);
    minorState = State::NotCollecting;
    minorSweepingFinished = false;
    majorStartedWhileMinorSweeping = false;
    majorFinishedWhileMinorSweeping = false;

#ifdef DEBUG
    for (BufferChunk* chunk : mixedChunks.ref()) {
      verifyChunk(chunk, true);
    }
    for (BufferChunk* chunk : tenuredChunks.ref()) {
      verifyChunk(chunk, false);
    }
#endif
  }

  if (majorSweepingFinished) {
    MOZ_ASSERT(majorState == State::Sweeping);
    majorState = State::NotCollecting;
    majorSweepingFinished = false;

    MOZ_ASSERT(tenuredChunksToSweep.ref().isEmpty());
  }
}

void BufferAllocator::clearMarkStateAfterBarrierVerification() {
  MOZ_ASSERT(!zone->wasGCStarted());

  maybeMergeSweptData();
  MOZ_ASSERT(minorState == State::NotCollecting);
  MOZ_ASSERT(majorState == State::NotCollecting);

  for (auto* chunks : {&mixedChunks.ref(), &tenuredChunks.ref()}) {
    for (auto* chunk : *chunks) {
      clearChunkMarkBits(chunk);
    }
  }

  for (auto* chunks :
       {&availableMixedChunks.ref(), &availableTenuredChunks.ref()}) {
    for (auto chunk = chunks->chunkIter(); !chunk.done(); chunk.next()) {
      clearChunkMarkBits(chunk);
    }
  }

#ifdef DEBUG
  checkGCStateNotInUse();
#endif
}

void BufferAllocator::clearChunkMarkBits(BufferChunk* chunk) {
  chunk->markBits.ref().clear();
  for (auto iter = chunk->smallRegionIter(); !iter.done(); iter.next()) {
    SmallBufferRegion* region = iter.get();
    region->markBits.ref().clear();
  }
}

bool BufferAllocator::isPointerWithinBuffer(void* ptr) {
  maybeMergeSweptData();

  MOZ_ASSERT(mixedChunksToSweep.ref().isEmpty());
  MOZ_ASSERT_IF(majorState != State::Marking,
                tenuredChunksToSweep.ref().isEmpty());

  for (const auto* chunks : {&mixedChunks.ref(), &tenuredChunks.ref(),
                             &tenuredChunksToSweep.ref()}) {
    for (auto* chunk : *chunks) {
      if (chunk->isPointerWithinAllocation(ptr)) {
        return true;
      }
    }
  }

  for (auto* chunks :
       {&availableMixedChunks.ref(), &availableTenuredChunks.ref()}) {
    for (auto chunk = chunks->chunkIter(); !chunk.done(); chunk.next()) {
      if (chunk->isPointerWithinAllocation(ptr)) {
        return true;
      }
    }
  }

  // Note we cannot safely access data that is being swept on another thread.

  for (const auto* allocs :
       {&largeNurseryAllocs.ref(), &largeTenuredAllocs.ref()}) {
    for (auto* alloc : *allocs) {
      if (alloc->isPointerWithinAllocation(ptr)) {
        return true;
      }
    }
  }

  return false;
}

bool BufferChunk::isPointerWithinAllocation(void* ptr) const {
  uintptr_t offset = uintptr_t(ptr) - uintptr_t(this);
  if (offset >= ChunkSize || offset < FirstMediumAllocOffset) {
    return false;
  }

  if (smallRegionBitmap.ref().getBit(offset / SmallRegionSize)) {
    auto* region = SmallBufferRegion::from(ptr);
    return region->isPointerWithinAllocation(ptr);
  }

  uintptr_t allocOffset =
      findPrevAllocated(RoundDown(offset, MinMediumAllocSize));
  MOZ_ASSERT(allocOffset <= ChunkSize);
  if (allocOffset == ChunkSize) {
    return false;
  }

  const void* alloc = ptrFromOffset(allocOffset);
  size_t size = allocBytes(alloc);
  return offset < allocOffset + size;
}

bool SmallBufferRegion::isPointerWithinAllocation(void* ptr) const {
  uintptr_t offset = uintptr_t(ptr) - uintptr_t(this);
  MOZ_ASSERT(offset < SmallRegionSize);

  uintptr_t allocOffset =
      findPrevAllocated(RoundDown(offset, SmallAllocGranularity));
  MOZ_ASSERT(allocOffset <= SmallRegionSize);
  if (allocOffset == SmallRegionSize) {
    return false;
  }

  const void* alloc = ptrFromOffset(allocOffset);
  size_t size = allocBytes(alloc);
  return offset < allocOffset + size;
}

bool LargeBuffer::isPointerWithinAllocation(void* ptr) const {
  return uintptr_t(ptr) - uintptr_t(alloc) < bytes;
}

#ifdef DEBUG

void BufferAllocator::checkGCStateNotInUse() {
  maybeMergeSweptData();
  AutoLock lock(this);  // Some fields are protected by this lock.
  checkGCStateNotInUse(lock);
}

void BufferAllocator::checkGCStateNotInUse(MaybeLock& maybeLock) {
  if (maybeLock.isNothing()) {
    // Some fields are protected by this lock.
    maybeLock.emplace(this);
  }

  checkGCStateNotInUse(maybeLock.ref());
}

void BufferAllocator::checkGCStateNotInUse(const AutoLock& lock) {
  MOZ_ASSERT(majorState == State::NotCollecting);
  bool isNurserySweeping = minorState == State::Sweeping;

  checkChunkListGCStateNotInUse(mixedChunks.ref(), true, false, false);
  checkChunkListGCStateNotInUse(tenuredChunks.ref(), false, false, false);
  checkChunkListsGCStateNotInUse(availableMixedChunks.ref(), true, false);
  checkChunkListsGCStateNotInUse(availableTenuredChunks.ref(), false, false);

  if (isNurserySweeping) {
    checkChunkListGCStateNotInUse(sweptMixedChunks.ref(), true,
                                  majorFinishedWhileMinorSweeping, true);
    checkChunkListGCStateNotInUse(sweptTenuredChunks.ref(), false, false, true);
  } else {
    MOZ_ASSERT(mixedChunksToSweep.ref().isEmpty());
    MOZ_ASSERT(largeNurseryAllocsToSweep.ref().isEmpty());

    MOZ_ASSERT(sweptMixedChunks.ref().isEmpty());
    MOZ_ASSERT(sweptTenuredChunks.ref().isEmpty());

    MOZ_ASSERT(!majorStartedWhileMinorSweeping);
    MOZ_ASSERT(!majorFinishedWhileMinorSweeping);
    MOZ_ASSERT(!hasMinorSweepDataToMerge);
    MOZ_ASSERT(!minorSweepingFinished);
    MOZ_ASSERT(!majorSweepingFinished);
  }

  MOZ_ASSERT(tenuredChunksToSweep.ref().isEmpty());

  checkAllocListGCStateNotInUse(largeNurseryAllocs.ref(), true);
  checkAllocListGCStateNotInUse(largeTenuredAllocs.ref(), false);

  MOZ_ASSERT(largeTenuredAllocsToSweep.ref().isEmpty());
  MOZ_ASSERT(sweptLargeTenuredAllocs.ref().isEmpty());
}

void BufferAllocator::checkChunkListsGCStateNotInUse(
    ChunkLists& chunkLists, bool hasNurseryOwnedAllocs,
    bool allowAllocatedDuringCollection) {
  for (auto chunk = chunkLists.chunkIter(); !chunk.done(); chunk.next()) {
    checkChunkGCStateNotInUse(chunk, allowAllocatedDuringCollection, true);
    verifyChunk(chunk, hasNurseryOwnedAllocs);

    MOZ_ASSERT(chunk->ownsFreeLists);
    size_t sizeClass = chunk.getSizeClass();

    MOZ_ASSERT(chunk->sizeClassForAvailableLists() == sizeClass);
    MOZ_ASSERT_IF(sizeClass != FullChunkSizeClass,
                  chunk->freeLists.ref().hasSizeClass(sizeClass));
  }
}

void BufferAllocator::checkChunkListGCStateNotInUse(
    BufferChunkList& chunks, bool hasNurseryOwnedAllocs,
    bool allowAllocatedDuringCollection, bool allowFreeLists) {
  for (BufferChunk* chunk : chunks) {
    checkChunkGCStateNotInUse(chunk, allowAllocatedDuringCollection,
                              allowFreeLists);
    verifyChunk(chunk, hasNurseryOwnedAllocs);
  }
}

void BufferAllocator::checkChunkGCStateNotInUse(
    BufferChunk* chunk, bool allowAllocatedDuringCollection,
    bool allowFreeLists) {
  MOZ_ASSERT_IF(!allowAllocatedDuringCollection,
                !chunk->allocatedDuringCollection);
  MOZ_ASSERT(chunk->markBits.ref().isEmpty());
  for (auto iter = chunk->smallRegionIter(); !iter.done(); iter.next()) {
    SmallBufferRegion* region = iter.get();
    MOZ_ASSERT(region->markBits.ref().isEmpty());
  }
  MOZ_ASSERT(allowFreeLists == chunk->ownsFreeLists);
  if (!chunk->ownsFreeLists) {
    chunk->freeLists.ref().assertEmpty();
  }
}

void BufferAllocator::verifyChunk(BufferChunk* chunk,
                                  bool hasNurseryOwnedAllocs) {
  MOZ_ASSERT(chunk->hasNurseryOwnedAllocs == hasNurseryOwnedAllocs);

  static constexpr size_t StepBytes = MediumAllocGranularity;

  size_t freeOffset = FirstMediumAllocOffset;

  size_t freeListsFreeRegionCount = 0;
  if (chunk->ownsFreeLists) {
    chunk->freeLists.ref().checkAvailable();
    for (auto region = chunk->freeLists.ref().freeRegionIter(); !region.done();
         region.next()) {
      MOZ_ASSERT(BufferChunk::from(region) == chunk);
      freeListsFreeRegionCount++;
    }
  } else {
    MOZ_ASSERT(chunk->freeLists.ref().isEmpty());
  }

  size_t chunkFreeRegionCount = 0;
  for (auto iter = chunk->allocIter(); !iter.done(); iter.next()) {
    // Check any free region preceding this allocation.
    size_t offset = iter.getOffset();
    MOZ_ASSERT(offset >= FirstMediumAllocOffset);
    if (offset > freeOffset) {
      verifyFreeRegion(chunk, offset, offset - freeOffset,
                       chunkFreeRegionCount);
    }

    // Check this allocation.
    void* alloc = iter.get();
    MOZ_ASSERT_IF(chunk->isNurseryOwned(alloc), hasNurseryOwnedAllocs);
    size_t bytes = chunk->allocBytes(alloc);
    uintptr_t endOffset = offset + bytes;
    MOZ_ASSERT(endOffset <= ChunkSize);
    for (size_t i = offset + StepBytes; i < endOffset; i += StepBytes) {
      MOZ_ASSERT(!chunk->isAllocated(i));
    }

    if (chunk->isSmallBufferRegion(alloc)) {
      auto* region = SmallBufferRegion::from(alloc);
      MOZ_ASSERT_IF(region->hasNurseryOwnedAllocs(), hasNurseryOwnedAllocs);
      verifySmallBufferRegion(region, chunkFreeRegionCount);
    }

    freeOffset = endOffset;
  }

  // Check any free region following the last allocation.
  if (freeOffset < ChunkSize) {
    verifyFreeRegion(chunk, ChunkSize, ChunkSize - freeOffset,
                     chunkFreeRegionCount);
  }

  MOZ_ASSERT_IF(chunk->ownsFreeLists,
                freeListsFreeRegionCount == chunkFreeRegionCount);
}

void BufferAllocator::verifyFreeRegion(BufferChunk* chunk, uintptr_t endOffset,
                                       size_t expectedSize,
                                       size_t& freeRegionCount) {
  MOZ_ASSERT(expectedSize >= MinFreeRegionSize);
  auto* freeRegion = FreeRegion::fromEndOffset(chunk, endOffset);
  MOZ_ASSERT(freeRegion->isInList());
  MOZ_ASSERT(freeRegion->size() == expectedSize);
  freeRegionCount++;
}

void BufferAllocator::verifySmallBufferRegion(SmallBufferRegion* region,
                                              size_t& freeRegionCount) {
  bool foundNurseryOwnedAllocs = false;

  static constexpr size_t StepBytes = SmallAllocGranularity;

  size_t freeOffset = FirstSmallAllocOffset;

  for (auto iter = region->allocIter(); !iter.done(); iter.next()) {
    // Check any free region preceding this allocation.
    size_t offset = iter.getOffset();
    MOZ_ASSERT(offset >= FirstSmallAllocOffset);
    if (offset > freeOffset) {
      verifyFreeRegion(region, offset, offset - freeOffset, freeRegionCount);
    }

    // Check this allocation.
    void* alloc = iter.get();
    MOZ_ASSERT_IF(region->isNurseryOwned(alloc),
                  region->hasNurseryOwnedAllocs());
    size_t bytes = region->allocBytes(alloc);
    uintptr_t endOffset = offset + bytes;
    MOZ_ASSERT(endOffset <= SmallRegionSize);
    for (size_t i = offset + StepBytes; i < endOffset; i += StepBytes) {
      MOZ_ASSERT(!region->isAllocated(i));
    }

    if (region->isNurseryOwned(alloc)) {
      foundNurseryOwnedAllocs = true;
    }

    freeOffset = endOffset;
  }

  MOZ_ASSERT(foundNurseryOwnedAllocs == region->hasNurseryOwnedAllocs());

  // Check any free region following the last allocation.
  if (freeOffset < SmallRegionSize) {
    verifyFreeRegion(region, SmallRegionSize, SmallRegionSize - freeOffset,
                     freeRegionCount);
  }
}

void BufferAllocator::verifyFreeRegion(SmallBufferRegion* region,
                                       uintptr_t endOffset, size_t expectedSize,
                                       size_t& freeRegionCount) {
  if (expectedSize < MinFreeRegionSize) {
    return;
  }

  auto* freeRegion = FreeRegion::fromEndOffset(region, endOffset);
  MOZ_ASSERT(freeRegion->isInList());
  MOZ_ASSERT(freeRegion->size() == expectedSize);
  freeRegionCount++;
}

void BufferAllocator::checkAllocListGCStateNotInUse(LargeAllocList& list,
                                                    bool isNurseryOwned) {
  for (LargeBuffer* buffer : list) {
    MOZ_ASSERT(buffer->isNurseryOwned == isNurseryOwned);
    MOZ_ASSERT_IF(!isNurseryOwned, !buffer->allocatedDuringCollection);
  }
}

#endif

void* BufferAllocator::allocSmall(size_t bytes, bool nurseryOwned, bool inGC) {
  MOZ_ASSERT(IsSmallAllocSize(bytes));

  // Round up to next available size.
  bytes = RoundUp(std::max(bytes, MinSmallAllocSize), SmallAllocGranularity);
  MOZ_ASSERT(bytes <= MaxSmallAllocSize);

  // Get size class from |bytes|.
  size_t sizeClass = SizeClassForSmallAlloc(bytes);

  void* alloc = bumpAlloc(bytes, sizeClass, MaxSmallAllocClass);
  if (MOZ_UNLIKELY(!alloc)) {
    alloc = retrySmallAlloc(bytes, sizeClass, inGC);
    if (!alloc) {
      return nullptr;
    }
  }

  SmallBufferRegion* region = SmallBufferRegion::from(alloc);
  region->setAllocated(alloc, bytes, true);
  MOZ_ASSERT(region->allocBytes(alloc) == bytes);

  MOZ_ASSERT(!region->isNurseryOwned(alloc));
  region->setNurseryOwned(alloc, nurseryOwned);

  auto* chunk = BufferChunk::from(alloc);
  if (nurseryOwned && !region->hasNurseryOwnedAllocs()) {
    region->setHasNurseryOwnedAllocs(true);
    setChunkHasNurseryAllocs(chunk);
  }

  // Heap size updates are done for the small buffer region as a whole, not
  // individual allocations within it.

  MOZ_ASSERT(!region->isMarked(alloc));
  MOZ_ASSERT(IsSmallAlloc(alloc));

  return alloc;
}

MOZ_NEVER_INLINE void* BufferAllocator::retrySmallAlloc(size_t bytes,
                                                        size_t sizeClass,
                                                        bool inGC) {
  auto alloc = [&]() {
    return bumpAlloc(bytes, sizeClass, MaxSmallAllocClass);
  };
  auto growHeap = [&]() { return allocNewSmallRegion(inGC); };

  return refillFreeListsAndRetryAlloc(sizeClass, MaxSmallAllocClass, alloc,
                                      growHeap);
}

bool BufferAllocator::allocNewSmallRegion(bool inGC) {
  void* ptr = allocMediumAligned(SmallRegionSize, inGC);
  if (!ptr) {
    return false;
  }

  auto* region = new (ptr) SmallBufferRegion;

  BufferChunk* chunk = BufferChunk::from(region);
  chunk->setSmallBufferRegion(region, true);

  uintptr_t freeStart = uintptr_t(region) + FirstSmallAllocOffset;
  uintptr_t freeEnd = uintptr_t(region) + SmallRegionSize;

  size_t sizeClass =
      SizeClassForFreeRegion(freeEnd - freeStart, SizeKind::Small);

  ptr = reinterpret_cast<void*>(freeEnd - sizeof(FreeRegion));
  FreeRegion* freeRegion = new (ptr) FreeRegion(freeStart);
  MOZ_ASSERT(freeRegion->getEnd() == freeEnd);
  freeLists.ref().pushFront(sizeClass, freeRegion);
  return true;
}

/* static */
bool BufferAllocator::IsSmallAlloc(void* alloc) {
  MOZ_ASSERT(IsBufferAlloc(alloc));

  // Test for large buffers before calling this so we can assume |alloc| is
  // inside a chunk.
  MOZ_ASSERT(!IsLargeAlloc(alloc));

  BufferChunk* chunk = BufferChunk::from(alloc);
  return chunk->isSmallBufferRegion(alloc);
}

void* BufferAllocator::allocMedium(size_t bytes, bool nurseryOwned, bool inGC) {
  MOZ_ASSERT(!IsSmallAllocSize(bytes));
  MOZ_ASSERT(!IsLargeAllocSize(bytes));

  // Round up to next allowed size.
  bytes = RoundUp(bytes, MediumAllocGranularity);
  MOZ_ASSERT(bytes <= MaxMediumAllocSize);

  // Get size class from |bytes|.
  size_t sizeClass = SizeClassForMediumAlloc(bytes);

  void* alloc = bumpAlloc(bytes, sizeClass, MaxMediumAllocClass);
  if (MOZ_UNLIKELY(!alloc)) {
    alloc = retryMediumAlloc(bytes, sizeClass, inGC);
    if (!alloc) {
      return nullptr;
    }
  }

  setAllocated(alloc, bytes, nurseryOwned, inGC);
  return alloc;
}

MOZ_NEVER_INLINE void* BufferAllocator::retryMediumAlloc(size_t bytes,
                                                         size_t sizeClass,
                                                         bool inGC) {
  auto alloc = [&]() {
    return bumpAlloc(bytes, sizeClass, MaxMediumAllocClass);
  };
  auto growHeap = [&]() { return allocNewChunk(inGC); };
  return refillFreeListsAndRetryAlloc(sizeClass, MaxMediumAllocClass, alloc,
                                      growHeap);
}

template <typename Alloc, typename GrowHeap>
void* BufferAllocator::refillFreeListsAndRetryAlloc(size_t sizeClass,
                                                    size_t maxSizeClass,
                                                    Alloc&& alloc,
                                                    GrowHeap&& growHeap) {
  for (;;) {
    RefillResult r = refillFreeLists(sizeClass, maxSizeClass, growHeap);
    if (r == RefillResult::Fail) {
      return nullptr;
    }

    if (r == RefillResult::Retry) {
      continue;
    }

    void* ptr = alloc();
    MOZ_ASSERT(ptr);
    return ptr;
  }
}

template <typename GrowHeap>
BufferAllocator::RefillResult BufferAllocator::refillFreeLists(
    size_t sizeClass, size_t maxSizeClass, GrowHeap&& growHeap) {
  MOZ_ASSERT(sizeClass <= maxSizeClass);

  // Take chunks from the available lists and add their free regions to the
  // free lists.
  if (useAvailableChunk(sizeClass, maxSizeClass)) {
    return RefillResult::Success;
  }

  // If that fails try to merge swept data and retry, avoiding taking the lock
  // unless we know there is data to merge. This reduces context switches.
  if (hasMinorSweepDataToMerge) {
    mergeSweptData();
    return RefillResult::Retry;
  }

  // If all else fails try to grow the heap.
  if (growHeap()) {
    return RefillResult::Success;
  }

  return RefillResult::Fail;
}

bool BufferAllocator::useAvailableChunk(size_t sizeClass, size_t maxSizeClass) {
  return useAvailableChunk(sizeClass, maxSizeClass, availableMixedChunks.ref(),
                           mixedChunks.ref()) ||
         useAvailableChunk(sizeClass, maxSizeClass,
                           availableTenuredChunks.ref(), tenuredChunks.ref());
}

bool BufferAllocator::useAvailableChunk(size_t sizeClass, size_t maxSizeClass,
                                        ChunkLists& src, BufferChunkList& dst) {
  // Move available chunks from available list |src| to current list |dst| (and
  // put their free regions into the |freeLists|) for size classes less than or
  // equal to |sizeClass| that are not currently represented in the free lists
  // and for which we have chunks in |src|.
  //
  // Chunks are moved from the available list to the free lists as needed to
  // limit the number of regions in the free lists, as these need to be iterated
  // on minor GC.
  //
  // This restriction on only moving regions less than or equal to the required
  // size class is to encourage filling up more used chunks before using less
  // used chunks, in the hope that less used chunks will become completely empty
  // and can be reclaimed.

  MOZ_ASSERT(freeLists.ref().getFirstAvailableSizeClass(
                 sizeClass, maxSizeClass) == SIZE_MAX);

  SizeClassBitSet sizeClasses = getChunkSizeClassesToMove(maxSizeClass, src);
  for (auto i = BitSetIter(sizeClasses); !i.done(); i.next()) {
    MOZ_ASSERT(i <= maxSizeClass);
    MOZ_ASSERT(!freeLists.ref().hasSizeClass(i));

    BufferChunk* chunk = src.popFirstChunk(i);
    MOZ_ASSERT(chunk->ownsFreeLists);
    MOZ_ASSERT(chunk->freeLists.ref().hasSizeClass(i));

    dst.pushBack(chunk);
    freeLists.ref().append(std::move(chunk->freeLists.ref()));
    chunk->ownsFreeLists = false;
    chunk->freeLists.ref().assertEmpty();

    if (i >= sizeClass) {
      // We should now be able to allocate a block of the required size as we've
      // added free regions of size class |i| where |i => sizeClass|.
      MOZ_ASSERT(freeLists.ref().getFirstAvailableSizeClass(
                     sizeClass, maxSizeClass) != SIZE_MAX);
      return true;
    }
  }

  MOZ_ASSERT(freeLists.ref().getFirstAvailableSizeClass(
                 sizeClass, maxSizeClass) == SIZE_MAX);
  return false;
}

BufferAllocator::SizeClassBitSet BufferAllocator::getChunkSizeClassesToMove(
    size_t maxSizeClass, ChunkLists& src) const {
  // Make a bitmap of size classes up to |maxSizeClass| which are not present in
  // |freeLists| but which are present in available chunks |src|.
  //
  // The ChunkLists bitmap has an extra bit to represent full chunks compared to
  // the FreeLists bitmap. This prevents using the classes methods, but since
  // they both fit into a single word we can manipulate the storage directly.
  SizeClassBitSet result;
  auto& sizeClasses = result.Storage()[0];
  auto& srcAvailable = src.availableSizeClasses().Storage()[0];
  auto& freeAvailable = freeLists.ref().availableSizeClasses().Storage()[0];
  sizeClasses = srcAvailable & ~freeAvailable & BitMask(maxSizeClass + 1);
  return result;
}

// Differentiate between small and medium size classes. Large allocations do not
// use size classes.
static bool IsMediumSizeClass(size_t sizeClass) {
  MOZ_ASSERT(sizeClass < BufferAllocator::AllocSizeClasses);
  return sizeClass >= MinMediumAllocClass;
}

/* static */
BufferAllocator::SizeKind BufferAllocator::SizeClassKind(size_t sizeClass) {
  return IsMediumSizeClass(sizeClass) ? SizeKind::Medium : SizeKind::Small;
}

void* BufferAllocator::bumpAlloc(size_t bytes, size_t sizeClass,
                                 size_t maxSizeClass) {
  MOZ_ASSERT(SizeClassKind(sizeClass) == SizeClassKind(maxSizeClass));
  freeLists.ref().checkAvailable();

  // Find smallest suitable size class that has free regions.
  sizeClass =
      freeLists.ref().getFirstAvailableSizeClass(sizeClass, maxSizeClass);
  if (sizeClass == SIZE_MAX) {
    return nullptr;
  }

  FreeRegion* region = freeLists.ref().getFirstRegion(sizeClass);
  MOZ_ASSERT(region->size() >= bytes);

  void* ptr = allocFromRegion(region, bytes, sizeClass);
  updateFreeListsAfterAlloc(&freeLists.ref(), region, sizeClass);
  return ptr;
}

#ifdef DEBUG
static size_t GranularityForSizeClass(size_t sizeClass) {
  return IsMediumSizeClass(sizeClass) ? MediumAllocGranularity
                                      : SmallAllocGranularity;
}
#endif  // DEBUG

void* BufferAllocator::allocFromRegion(FreeRegion* region, size_t bytes,
                                       size_t sizeClass) {
  uintptr_t start = region->startAddr;
  MOZ_ASSERT(region->getEnd() > start);
  MOZ_ASSERT_IF(sizeClass != MaxMediumAllocClass,
                region->size() >= SizeClassBytes(sizeClass));
  MOZ_ASSERT_IF(sizeClass == MaxMediumAllocClass,
                region->size() >= MaxMediumAllocSize);
  MOZ_ASSERT(start % GranularityForSizeClass(sizeClass) == 0);
  MOZ_ASSERT(region->size() % GranularityForSizeClass(sizeClass) == 0);

  // Ensure whole region is commited.
  if (region->hasDecommittedPages) {
    recommitRegion(region);
  }

  // Allocate from start of region.
  void* ptr = reinterpret_cast<void*>(start);
  start += bytes;
  MOZ_ASSERT(region->getEnd() >= start);

  // Update region start.
  region->startAddr = start;

  return ptr;
}

// Allocate a region of size |bytes| aligned to |bytes|. The maximum size is
// limited to 256KB. In practice this is only ever used to allocate
// SmallBufferRegions.
void* BufferAllocator::allocMediumAligned(size_t bytes, bool inGC) {
  MOZ_ASSERT(bytes >= MinMediumAllocSize);
  MOZ_ASSERT(bytes <= MaxAlignedAllocSize);
  MOZ_ASSERT(mozilla::IsPowerOfTwo(bytes));

  // Get size class from |bytes|.
  size_t sizeClass = SizeClassForMediumAlloc(bytes);

  void* alloc = alignedAlloc(sizeClass);
  if (MOZ_UNLIKELY(!alloc)) {
    alloc = retryAlignedAlloc(sizeClass, inGC);
    if (!alloc) {
      return nullptr;
    }
  }

  setAllocated(alloc, bytes, false, inGC);

  return alloc;
}

MOZ_NEVER_INLINE void* BufferAllocator::retryAlignedAlloc(size_t sizeClass,
                                                          bool inGC) {
  auto alloc = [&]() { return alignedAlloc(sizeClass); };
  auto growHeap = [&]() { return allocNewChunk(inGC); };
  return refillFreeListsAndRetryAlloc(sizeClass + 1, MaxMediumAllocClass, alloc,
                                      growHeap);
}

void* BufferAllocator::alignedAlloc(size_t sizeClass) {
  freeLists.ref().checkAvailable();

  // Try the first free region for the smallest possible size class. This will
  // fail if that region is for the exact size class requested but the region is
  // not aligned.
  size_t allocClass = freeLists.ref().getFirstAvailableSizeClass(
      sizeClass, MaxMediumAllocClass);
  MOZ_ASSERT(allocClass >= sizeClass);
  if (allocClass == SIZE_MAX) {
    return nullptr;
  }

  FreeRegion* region = freeLists.ref().getFirstRegion(allocClass);
  void* ptr = alignedAllocFromRegion(region, sizeClass);
  if (ptr) {
    updateFreeListsAfterAlloc(&freeLists.ref(), region, allocClass);
    return ptr;
  }

  // If we couldn't allocate an aligned region, try a larger size class. This
  // only happens if we selected the size class equal to the requested size.
  MOZ_ASSERT(allocClass == sizeClass);
  allocClass = freeLists.ref().getFirstAvailableSizeClass(sizeClass + 1,
                                                          MaxMediumAllocClass);
  if (allocClass == SIZE_MAX) {
    return nullptr;
  }

  region = freeLists.ref().getFirstRegion(allocClass);
  ptr = alignedAllocFromRegion(region, sizeClass);
  MOZ_ASSERT(ptr);
  updateFreeListsAfterAlloc(&freeLists.ref(), region, allocClass);
  return ptr;
}

void* BufferAllocator::alignedAllocFromRegion(FreeRegion* region,
                                              size_t sizeClass) {
  // Attempt to allocate an aligned region from |region|.

  uintptr_t start = region->startAddr;
  MOZ_ASSERT(region->getEnd() > start);
  MOZ_ASSERT(region->size() >= SizeClassBytes(sizeClass));
  MOZ_ASSERT((region->size() % MinMediumAllocSize) == 0);

  size_t bytes = SizeClassBytes(sizeClass);
  size_t alignedStart = RoundUp(start, bytes);
  size_t end = alignedStart + bytes;
  if (end > region->getEnd()) {
    return nullptr;
  }

  // Align the start of the region, creating a new free region out of the space
  // at the start if necessary.
  if (alignedStart != start) {
    size_t alignBytes = alignedStart - start;
    void* prefix = allocFromRegion(region, alignBytes, sizeClass);
    MOZ_ASSERT(uintptr_t(prefix) == start);
    (void)prefix;
    MOZ_ASSERT(!region->hasDecommittedPages);
    addFreeRegion(&freeLists.ref(), start, alignBytes, SizeKind::Medium, false,
                  ListPosition::Back);
  }

  // Now the start is aligned we can use the normal allocation method.
  MOZ_ASSERT(region->startAddr % bytes == 0);
  return allocFromRegion(region, bytes, sizeClass);
}

void BufferAllocator::setAllocated(void* alloc, size_t bytes, bool nurseryOwned,
                                   bool inGC) {
  BufferChunk* chunk = BufferChunk::from(alloc);
  chunk->setAllocated(alloc, bytes, true);
  MOZ_ASSERT(chunk->allocBytes(alloc) == bytes);

  MOZ_ASSERT(!chunk->isNurseryOwned(alloc));
  chunk->setNurseryOwned(alloc, nurseryOwned);

  if (nurseryOwned) {
    setChunkHasNurseryAllocs(chunk);
  }

  MOZ_ASSERT(!chunk->isMarked(alloc));

  if (!nurseryOwned) {
    bool checkThresholds = !inGC;
    updateHeapSize(bytes, checkThresholds, false);
  }

  MOZ_ASSERT(!chunk->isSmallBufferRegion(alloc));
}

void BufferAllocator::setChunkHasNurseryAllocs(BufferChunk* chunk) {
  MOZ_ASSERT(!chunk->ownsFreeLists);

  if (chunk->hasNurseryOwnedAllocs) {
    return;
  }

  tenuredChunks.ref().remove(chunk);
  mixedChunks.ref().pushBack(chunk);
  chunk->hasNurseryOwnedAllocs = true;
}

void BufferAllocator::updateFreeListsAfterAlloc(FreeLists* freeLists,
                                                FreeRegion* region,
                                                size_t sizeClass) {
  // Updates |freeLists| after an allocation from |region| which is currently in
  // the |sizeClass| free list. This may move the region to a different free
  // list.

  freeLists->assertContains(sizeClass, region);

  // If the region is still valid for further allocations of this size class
  // then there's nothing to do.
  size_t classBytes = SizeClassBytes(sizeClass);
  size_t newSize = region->size();
  MOZ_ASSERT(newSize % GranularityForSizeClass(sizeClass) == 0);
  if (newSize >= classBytes) {
    return;
  }

  // Remove region from this free list.
  freeLists->remove(sizeClass, region);

  // If the region is now empty then we're done.
  if (newSize == 0) {
    return;
  }

  // Otherwise region is now too small. Move it to the appropriate bucket for
  // its reduced size if possible.

  if (newSize < MinFreeRegionSize) {
    // We can't record a region this small. The free space will not be reused
    // until enough adjacent space become free
    return;
  }

  size_t newSizeClass =
      SizeClassForFreeRegion(newSize, SizeClassKind(sizeClass));
  MOZ_ASSERT_IF(newSizeClass != MaxMediumAllocClass,
                newSize >= SizeClassBytes(newSizeClass));
  MOZ_ASSERT(newSizeClass <= sizeClass);
  MOZ_ASSERT_IF(newSizeClass != MaxMediumAllocClass, newSizeClass < sizeClass);
  MOZ_ASSERT(SizeClassKind(newSizeClass) == SizeClassKind(sizeClass));
  freeLists->pushFront(newSizeClass, region);
}

void BufferAllocator::recommitRegion(FreeRegion* region) {
  MOZ_ASSERT(region->hasDecommittedPages);
  MOZ_ASSERT(DecommitEnabled());

  BufferChunk* chunk = BufferChunk::from(region);
  uintptr_t startAddr = RoundUp(region->startAddr, PageSize);
  uintptr_t endAddr = RoundDown(uintptr_t(region), PageSize);

  size_t startPage = (startAddr - uintptr_t(chunk)) / PageSize;
  size_t endPage = (endAddr - uintptr_t(chunk)) / PageSize;

  // If the start of the region does not lie on a page boundary the page it is
  // in should be committed as it must either contain the start of the chunk, a
  // FreeRegion or an allocation.
  MOZ_ASSERT_IF((region->startAddr % PageSize) != 0,
                !chunk->decommittedPages.ref()[startPage - 1]);

  // The end of the region should be committed as it holds FreeRegion |region|.
  MOZ_ASSERT(!chunk->decommittedPages.ref()[endPage]);

  MarkPagesInUseSoft(reinterpret_cast<void*>(startAddr), endAddr - startAddr);
  for (size_t i = startPage; i != endPage; i++) {
    chunk->decommittedPages.ref()[i] = false;
  }

  region->hasDecommittedPages = false;
}

static inline StallAndRetry ShouldStallAndRetry(bool inGC) {
  return inGC ? StallAndRetry::Yes : StallAndRetry::No;
}

bool BufferAllocator::allocNewChunk(bool inGC) {
  GCRuntime* gc = &zone->runtimeFromMainThread()->gc;
  AutoLockGCBgAlloc lock(gc);
  ArenaChunk* baseChunk = gc->getOrAllocChunk(ShouldStallAndRetry(inGC), lock);
  if (!baseChunk) {
    return false;
  }

  CheckHighBitsOfPointer(baseChunk);

  // Ensure all memory is initially committed.
  if (!baseChunk->decommittedPages.IsEmpty()) {
    MOZ_ASSERT(DecommitEnabled());
    MarkPagesInUseSoft(baseChunk, ChunkSize);
  }

  // Unpoison past the ChunkBase header.
  void* ptr = reinterpret_cast<void*>(uintptr_t(baseChunk) + sizeof(ChunkBase));
  size_t size = ChunkSize - sizeof(ChunkBase);
  SetMemCheckKind(ptr, size, MemCheckKind::MakeUndefined);

  BufferChunk* chunk = new (baseChunk) BufferChunk(zone);
  chunk->allocatedDuringCollection = majorState != State::NotCollecting;

  tenuredChunks.ref().pushBack(chunk);

  uintptr_t freeStart = uintptr_t(chunk) + FirstMediumAllocOffset;
  uintptr_t freeEnd = uintptr_t(chunk) + ChunkSize;

  size_t sizeClass =
      SizeClassForFreeRegion(freeEnd - freeStart, SizeKind::Medium);
  MOZ_ASSERT(sizeClass > MaxSmallAllocClass);
  MOZ_ASSERT(sizeClass <= MaxMediumAllocClass);

  ptr = reinterpret_cast<void*>(freeEnd - sizeof(FreeRegion));
  FreeRegion* region = new (ptr) FreeRegion(freeStart);
  MOZ_ASSERT(region->getEnd() == freeEnd);
  freeLists.ref().pushFront(sizeClass, region);

  return true;
}

static void SetDeallocated(BufferChunk* chunk, void* alloc, size_t bytes) {
  MOZ_ASSERT(!chunk->isSmallBufferRegion(alloc));
  MOZ_ASSERT(chunk->allocBytes(alloc) == bytes);
  chunk->setNurseryOwned(alloc, false);
  chunk->setAllocated(alloc, bytes, false);
}

static void SetDeallocated(SmallBufferRegion* region, void* alloc,
                           size_t bytes) {
  MOZ_ASSERT(region->allocBytes(alloc) == bytes);
  region->setNurseryOwned(alloc, false);
  region->setAllocated(alloc, bytes, false);
}

bool BufferAllocator::sweepChunk(BufferChunk* chunk, SweepKind sweepKind,
                                 bool shouldDecommit) {
  // Find all regions of free space in |chunk| and add them to the swept free
  // lists.

  // TODO: It could be beneficialy to allocate from most-full chunks first. This
  // could happen by sweeping all chunks and then sorting them by how much free
  // space they had and then adding their free regions to the free lists in that
  // order.

  MOZ_ASSERT_IF(sweepKind == SweepKind::Tenured,
                !chunk->allocatedDuringCollection);
  MOZ_ASSERT_IF(sweepKind == SweepKind::Tenured, chunk->ownsFreeLists);
  FreeLists& freeLists = chunk->freeLists.ref();

  // TODO: For tenured sweeping, check whether anything needs to be swept and
  // reuse the existing free regions rather than rebuilding these every time.
  freeLists.clear();
  chunk->ownsFreeLists = true;

  GCRuntime* gc = &zone->runtimeFromAnyThread()->gc;

  bool hasNurseryOwnedAllocs = false;

  size_t freeStart = FirstMediumAllocOffset;
  bool sweptAny = false;
  size_t mallocHeapBytesFreed = 0;

  // First sweep any small buffer regions.
  for (auto iter = chunk->smallRegionIter(); !iter.done(); iter.next()) {
    SmallBufferRegion* region = iter.get();
    MOZ_ASSERT(!chunk->isMarked(region));
    MOZ_ASSERT(chunk->allocBytes(region) == SmallRegionSize);

    if (!sweepSmallBufferRegion(chunk, region, sweepKind)) {
      chunk->setSmallBufferRegion(region, false);
      SetDeallocated(chunk, region, SmallRegionSize);
      PoisonAlloc(region, JS_SWEPT_TENURED_PATTERN, sizeof(SmallBufferRegion),
                  MemCheckKind::MakeUndefined);
      mallocHeapBytesFreed += SmallRegionSize;
      sweptAny = true;
    } else if (region->hasNurseryOwnedAllocs()) {
      hasNurseryOwnedAllocs = true;
    }
  }

  for (auto iter = chunk->allocIter(); !iter.done(); iter.next()) {
    void* alloc = iter.get();

    size_t bytes = chunk->allocBytes(alloc);
    uintptr_t allocEnd = iter.getOffset() + bytes;

    bool nurseryOwned = chunk->isNurseryOwned(alloc);
    bool canSweep = !chunk->isSmallBufferRegion(alloc) &&
                    CanSweepAlloc(nurseryOwned, sweepKind);

    bool shouldSweep = canSweep && !chunk->isMarked(alloc);
    if (shouldSweep) {
      // Dead. Update allocated bitmap, metadata and heap size accounting.
      if (!nurseryOwned) {
        mallocHeapBytesFreed += bytes;
      }
      SetDeallocated(chunk, alloc, bytes);
      PoisonAlloc(alloc, JS_SWEPT_TENURED_PATTERN, bytes,
                  MemCheckKind::MakeUndefined);
      sweptAny = true;
    } else {
      // Alive. Add any free space before this allocation.
      uintptr_t allocStart = iter.getOffset();
      if (freeStart != allocStart) {
        addSweptRegion(chunk, freeStart, allocStart, shouldDecommit, !sweptAny,
                       freeLists);
      }
      freeStart = allocEnd;
      if (canSweep) {
        chunk->setUnmarked(alloc);
      }
      if (nurseryOwned) {
        MOZ_ASSERT(sweepKind == SweepKind::Nursery);
        hasNurseryOwnedAllocs = true;
      }
    }
  }

  if (mallocHeapBytesFreed) {
    bool inMajorGC = sweepKind == SweepKind::Tenured;
    zone->mallocHeapSize.removeBytes(mallocHeapBytesFreed, inMajorGC);
  }

  if (freeStart == FirstMediumAllocOffset) {
    // Chunk is empty. Give it back to the system.
    bool allMemoryCommitted = chunk->decommittedPages.ref().IsEmpty();
    chunk->~BufferChunk();
    ArenaChunk* tenuredChunk = ArenaChunk::init(chunk, gc, allMemoryCommitted);
    AutoLockGC lock(gc);
    gc->recycleChunk(tenuredChunk, lock);
    return false;
  }

  // Add any free space from the last allocation to the end of the chunk.
  if (freeStart != ChunkSize) {
    addSweptRegion(chunk, freeStart, ChunkSize, shouldDecommit, !sweptAny,
                   freeLists);
  }

  chunk->hasNurseryOwnedAllocsAfterSweep = hasNurseryOwnedAllocs;

  return true;
}

/* static */
bool BufferAllocator::CanSweepAlloc(bool nurseryOwned,
                                    BufferAllocator::SweepKind sweepKind) {
  static_assert(SweepKind::Nursery == SweepKind(uint8_t(true)));
  static_assert(SweepKind::Tenured == SweepKind(uint8_t(false)));
  SweepKind requiredKind = SweepKind(uint8_t(nurseryOwned));
  return sweepKind == requiredKind;
}

void BufferAllocator::addSweptRegion(BufferChunk* chunk, uintptr_t freeStart,
                                     uintptr_t freeEnd, bool shouldDecommit,
                                     bool expectUnchanged,
                                     FreeLists& freeLists) {
  // Add the region from |freeStart| to |freeEnd| to the appropriate swept free
  // list based on its size.

  MOZ_ASSERT(freeStart >= FirstMediumAllocOffset);
  MOZ_ASSERT(freeStart < freeEnd);
  MOZ_ASSERT(freeEnd <= ChunkSize);
  MOZ_ASSERT((freeStart % MediumAllocGranularity) == 0);
  MOZ_ASSERT((freeEnd % MediumAllocGranularity) == 0);
  MOZ_ASSERT_IF(shouldDecommit, DecommitEnabled());

  // Decommit pages if |shouldDecommit| was specified, but leave space for
  // the FreeRegion structure at the end.
  bool anyDecommitted = false;
  uintptr_t decommitStart = RoundUp(freeStart, PageSize);
  uintptr_t decommitEnd = RoundDown(freeEnd - sizeof(FreeRegion), PageSize);
  size_t endPage = decommitEnd / PageSize;
  if (shouldDecommit && decommitEnd > decommitStart) {
    void* ptr = reinterpret_cast<void*>(decommitStart + uintptr_t(chunk));
    MarkPagesUnusedSoft(ptr, decommitEnd - decommitStart);
    size_t startPage = decommitStart / PageSize;
    for (size_t i = startPage; i != endPage; i++) {
      chunk->decommittedPages.ref()[i] = true;
    }
    anyDecommitted = true;
  } else {
    // Check for any previously decommitted pages.
    uintptr_t startPage = RoundDown(freeStart, PageSize) / PageSize;
    for (size_t i = startPage; i != endPage; i++) {
      if (chunk->decommittedPages.ref()[i]) {
        anyDecommitted = true;
      }
    }
  }

  // The last page must have previously been either a live allocation or a
  // FreeRegion, so it must already be committed.
  MOZ_ASSERT(!chunk->decommittedPages.ref()[endPage]);

  freeStart += uintptr_t(chunk);
  freeEnd += uintptr_t(chunk);

  size_t bytes = freeEnd - freeStart;
  addFreeRegion(&freeLists, freeStart, bytes, SizeKind::Medium, anyDecommitted,
                ListPosition::Back, expectUnchanged);
}

bool BufferAllocator::sweepSmallBufferRegion(BufferChunk* chunk,
                                             SmallBufferRegion* region,
                                             SweepKind sweepKind) {
  bool hasNurseryOwnedAllocs = false;

  FreeLists& freeLists = chunk->freeLists.ref();

  size_t freeStart = FirstSmallAllocOffset;
  bool sweptAny = false;

  for (auto iter = region->allocIter(); !iter.done(); iter.next()) {
    void* alloc = iter.get();

    size_t bytes = region->allocBytes(alloc);
    uintptr_t allocEnd = iter.getOffset() + bytes;

    bool nurseryOwned = region->isNurseryOwned(alloc);
    bool canSweep = CanSweepAlloc(nurseryOwned, sweepKind);

    bool shouldSweep = canSweep && !region->isMarked(alloc);
    if (shouldSweep) {
      // Dead. Update allocated bitmap, metadata and heap size accounting.
      SetDeallocated(region, alloc, bytes);
      PoisonAlloc(alloc, JS_SWEPT_TENURED_PATTERN, bytes,
                  MemCheckKind::MakeUndefined);
      sweptAny = true;
    } else {
      // Alive. Add any free space before this allocation.
      uintptr_t allocStart = iter.getOffset();
      if (freeStart != allocStart) {
        addSweptRegion(region, freeStart, allocStart, !sweptAny, freeLists);
      }
      freeStart = allocEnd;
      if (canSweep) {
        region->setUnmarked(alloc);
      }
      if (nurseryOwned) {
        MOZ_ASSERT(sweepKind == SweepKind::Nursery);
        hasNurseryOwnedAllocs = true;
      }
      sweptAny = false;
    }
  }

  if (freeStart == FirstSmallAllocOffset) {
    // Region is empty.
    return false;
  }

  // Add any free space from the last allocation to the end of the chunk.
  if (freeStart != SmallRegionSize) {
    addSweptRegion(region, freeStart, SmallRegionSize, !sweptAny, freeLists);
  }

  region->setHasNurseryOwnedAllocs(hasNurseryOwnedAllocs);

  return true;
}

void BufferAllocator::addSweptRegion(SmallBufferRegion* region,
                                     uintptr_t freeStart, uintptr_t freeEnd,
                                     bool expectUnchanged,
                                     FreeLists& freeLists) {
  // Add the region from |freeStart| to |freeEnd| to the appropriate swept free
  // list based on its size. Unused pages in small buffer regions are not
  // decommitted.

  MOZ_ASSERT(freeStart >= FirstSmallAllocOffset);
  MOZ_ASSERT(freeStart < freeEnd);
  MOZ_ASSERT(freeEnd <= SmallRegionSize);
  MOZ_ASSERT(freeStart % SmallAllocGranularity == 0);
  MOZ_ASSERT(freeEnd % SmallAllocGranularity == 0);

  freeStart += uintptr_t(region);
  freeEnd += uintptr_t(region);

  size_t bytes = freeEnd - freeStart;
  addFreeRegion(&freeLists, freeStart, bytes, SizeKind::Small, false,
                ListPosition::Back, expectUnchanged);
}

void BufferAllocator::freeMedium(void* alloc) {
  // Free a medium sized allocation. This coalesces the free space with any
  // neighboring free regions. Coalescing is necessary for resize to work
  // properly.

  BufferChunk* chunk = BufferChunk::from(alloc);
  MOZ_ASSERT(chunk->zone == zone);

  size_t bytes = chunk->allocBytes(alloc);
  PoisonAlloc(alloc, JS_FREED_BUFFER_PATTERN, bytes,
              MemCheckKind::MakeUndefined);

  if (isSweepingChunk(chunk)) {
    return;  // We can't free if the chunk is currently being swept.
  }

  // Update heap size for tenured owned allocations.
  if (!chunk->isNurseryOwned(alloc)) {
    bool updateRetained =
        majorState == State::Marking && !chunk->allocatedDuringCollection;
    zone->mallocHeapSize.removeBytes(bytes, updateRetained);
  }

  // TODO: Since the mark bits are atomic, it's probably OK to unmark even if
  // the chunk is currently being swept. If we get lucky the memory will be
  // freed sooner.
  chunk->setUnmarked(alloc);

  // Set region as not allocated and clear metadata.
  SetDeallocated(chunk, alloc, bytes);

  FreeLists* freeLists = getChunkFreeLists(chunk);

  uintptr_t startAddr = uintptr_t(alloc);
  uintptr_t endAddr = startAddr + bytes;

  // If the chunk is in one of the available lists we may need to move it.
  ChunkLists* availableChunks = getChunkAvailableLists(chunk);
  size_t oldChunkSizeClass = SIZE_MAX;
  if (availableChunks) {
    oldChunkSizeClass = chunk->sizeClassForAvailableLists();
  }

  // First check whether there is a free region following the allocation.
  FreeRegion* region;
  uintptr_t endOffset = endAddr & ChunkMask;
  if (endOffset == 0 || chunk->isAllocated(endOffset)) {
    // The allocation abuts the end of the chunk or another allocation. Add the
    // allocation as a new free region.
    //
    // The new region is added to the front of relevant list so as to reuse
    // recently freed memory preferentially. This may reduce fragmentation. See
    // "The Memory Fragmentation Problem: Solved?"  by Johnstone et al.
    region = addFreeRegion(freeLists, startAddr, bytes, SizeKind::Medium, false,
                           ListPosition::Front);
    MOZ_ASSERT(region);  // Always succeeds for medium allocations.
  } else {
    // There is a free region following this allocation. Expand the existing
    // region down to cover the newly freed space.
    region = chunk->findFollowingFreeRegion(endAddr);
    MOZ_ASSERT(region->startAddr == endAddr);
    updateFreeRegionStart(freeLists, region, startAddr, SizeKind::Medium);
  }

  // Next check for any preceding free region and coalesce.
  FreeRegion* precRegion = chunk->findPrecedingFreeRegion(startAddr);
  if (precRegion) {
    if (freeLists) {
      size_t sizeClass =
          SizeClassForFreeRegion(precRegion->size(), SizeKind::Medium);
      freeLists->remove(sizeClass, precRegion);
    }

    updateFreeRegionStart(freeLists, region, precRegion->startAddr,
                          SizeKind::Medium);
    if (precRegion->hasDecommittedPages) {
      region->hasDecommittedPages = true;
    }
  }

  if (availableChunks) {
    maybeUpdateAvailableLists(availableChunks, chunk, oldChunkSizeClass);
  }
}

void BufferAllocator::maybeUpdateAvailableLists(ChunkLists* availableChunks,
                                                BufferChunk* chunk,
                                                size_t oldChunkSizeClass) {
  // A realloc or free operation can change the amount of free space in an
  // available chunk, so we may need to move it to a different list.
  size_t newChunkSizeClass = chunk->sizeClassForAvailableLists();
  if (newChunkSizeClass != oldChunkSizeClass) {
    availableChunks->remove(oldChunkSizeClass, chunk);
    availableChunks->pushBack(newChunkSizeClass, chunk);
  }
}

bool BufferAllocator::isSweepingChunk(BufferChunk* chunk) {
  if (minorState == State::Sweeping && chunk->hasNurseryOwnedAllocs) {
    // We are currently sweeping nursery owned allocations.

    // TODO: We could set a flag for nursery chunks allocated during minor
    // collection to allow operations on chunks that are not being swept here.

    if (!hasMinorSweepDataToMerge) {
#ifdef DEBUG
      {
        AutoLock lock(this);
        MOZ_ASSERT_IF(!hasMinorSweepDataToMerge, !minorSweepingFinished);
      }
#endif

      // Likely no data to merge so don't bother taking the lock.
      return true;
    }

    // Merge swept data and recheck.
    //
    // TODO: It would be good to know how often this helps and if it is
    // worthwhile.
    mergeSweptData();
    if (minorState == State::Sweeping && chunk->hasNurseryOwnedAllocs) {
      return true;
    }
  }

  if (majorState == State::Sweeping && !chunk->allocatedDuringCollection) {
    // We are currently sweeping tenured owned allocations.
    return true;
  }

  return false;
}

BufferAllocator::FreeRegion* BufferAllocator::addFreeRegion(
    FreeLists* freeLists, uintptr_t start, size_t bytes, SizeKind kind,
    bool anyDecommitted, ListPosition position,
    bool expectUnchanged /* = false */) {
  static_assert(sizeof(FreeRegion) <= MinFreeRegionSize);
  if (bytes < MinFreeRegionSize) {
    // We can't record a region this small. The free space will not be reused
    // until enough adjacent space become free.
    return nullptr;
  }

  size_t sizeClass = SizeClassForFreeRegion(bytes, kind);
  MOZ_ASSERT_IF(sizeClass != MaxMediumAllocClass,
                bytes >= SizeClassBytes(sizeClass));

  MOZ_ASSERT(start % GranularityForSizeClass(sizeClass) == 0);
  MOZ_ASSERT(bytes % GranularityForSizeClass(sizeClass) == 0);

  uintptr_t end = start + bytes;
#ifdef DEBUG
  if (expectUnchanged) {
    // We didn't free any allocations so there should already be a FreeRegion
    // from |start| to |end|.
    auto* region = FreeRegion::fromEndAddr(end);
    MOZ_ASSERT(region->startAddr == start);
  }
#endif

  void* ptr = reinterpret_cast<void*>(end - sizeof(FreeRegion));
  FreeRegion* region = new (ptr) FreeRegion(start, anyDecommitted);
  MOZ_ASSERT(region->getEnd() == end);

  if (freeLists) {
    if (position == ListPosition::Front) {
      freeLists->pushFront(sizeClass, region);
    } else {
      freeLists->pushBack(sizeClass, region);
    }
  }

  return region;
}

void BufferAllocator::updateFreeRegionStart(FreeLists* freeLists,
                                            FreeRegion* region,
                                            uintptr_t newStart, SizeKind kind) {
  MOZ_ASSERT((newStart & ~ChunkMask) == (uintptr_t(region) & ~ChunkMask));
  MOZ_ASSERT(region->startAddr != newStart);

  // TODO: Support realloc for small regions.
  MOZ_ASSERT(kind == SizeKind::Medium);

  size_t oldSize = region->size();
  region->startAddr = newStart;

  if (!freeLists) {
    return;
  }

  size_t currentSizeClass = SizeClassForFreeRegion(oldSize, kind);
  size_t newSizeClass = SizeClassForFreeRegion(region->size(), kind);
  MOZ_ASSERT(SizeClassKind(newSizeClass) == SizeClassKind(currentSizeClass));
  if (currentSizeClass != newSizeClass) {
    freeLists->remove(currentSizeClass, region);
    freeLists->pushFront(newSizeClass, region);
  }
}

bool BufferAllocator::growMedium(void* alloc, size_t newBytes) {
  MOZ_ASSERT(!IsSmallAllocSize(newBytes));
  MOZ_ASSERT(!IsLargeAllocSize(newBytes));
  newBytes = std::max(newBytes, MinMediumAllocSize);
  MOZ_ASSERT(newBytes == GetGoodAllocSize(newBytes));

  BufferChunk* chunk = BufferChunk::from(alloc);
  MOZ_ASSERT(chunk->zone == zone);

  if (isSweepingChunk(chunk)) {
    return false;  // We can't grow if the chunk is currently being swept.
  }

  size_t currentBytes = chunk->allocBytes(alloc);
  MOZ_ASSERT(newBytes > currentBytes);

  uintptr_t endOffset = (uintptr_t(alloc) & ChunkMask) + currentBytes;
  MOZ_ASSERT(endOffset <= ChunkSize);
  if (endOffset == ChunkSize) {
    return false;  // Can't extend because we're at the end of the chunk.
  }

  size_t endAddr = uintptr_t(chunk) + endOffset;
  if (chunk->isAllocated(endOffset)) {
    return false;  // Can't extend because we abut another allocation.
  }

  FreeRegion* region = chunk->findFollowingFreeRegion(endAddr);
  MOZ_ASSERT(region->startAddr == endAddr);

  size_t extraBytes = newBytes - currentBytes;
  if (region->size() < extraBytes) {
    return false;  // Can't extend because following free region is too small.
  }

  size_t sizeClass = SizeClassForFreeRegion(region->size(), SizeKind::Medium);

  allocFromRegion(region, extraBytes, sizeClass);

  // If the chunk is in one of the available lists we may need to move it if the
  // largest free region has shrunk too much.
  ChunkLists* availableChunks = getChunkAvailableLists(chunk);
  size_t oldChunkSizeClass = SIZE_MAX;
  if (availableChunks) {
    oldChunkSizeClass = chunk->sizeClassForAvailableLists();
  }

  FreeLists* freeLists = getChunkFreeLists(chunk);
  updateFreeListsAfterAlloc(freeLists, region, sizeClass);

  if (availableChunks) {
    maybeUpdateAvailableLists(availableChunks, chunk, oldChunkSizeClass);
  }

  chunk->updateEndOffset(alloc, currentBytes, newBytes);
  MOZ_ASSERT(chunk->allocBytes(alloc) == newBytes);

  if (!chunk->isNurseryOwned(alloc)) {
    bool updateRetained =
        majorState == State::Marking && !chunk->allocatedDuringCollection;
    updateHeapSize(extraBytes, true, updateRetained);
  }

  return true;
}

bool BufferAllocator::shrinkMedium(void* alloc, size_t newBytes) {
  MOZ_ASSERT(!IsSmallAllocSize(newBytes));
  MOZ_ASSERT(!IsLargeAllocSize(newBytes));
  newBytes = std::max(newBytes, MinMediumAllocSize);
  MOZ_ASSERT(newBytes == GetGoodAllocSize(newBytes));

  BufferChunk* chunk = BufferChunk::from(alloc);
  MOZ_ASSERT(chunk->zone == zone);

  if (isSweepingChunk(chunk)) {
    return false;  // We can't shrink if the chunk is currently being swept.
  }

  size_t currentBytes = chunk->allocBytes(alloc);
  if (newBytes == currentBytes) {
    // Requested size is the same after adjusting to a valid medium alloc size.
    return false;
  }

  MOZ_ASSERT(newBytes < currentBytes);
  size_t sizeChange = currentBytes - newBytes;

  // Update allocation size.
  chunk->updateEndOffset(alloc, currentBytes, newBytes);
  MOZ_ASSERT(chunk->allocBytes(alloc) == newBytes);
  if (!chunk->isNurseryOwned(alloc)) {
    bool updateRetained =
        majorState == State::Marking && !chunk->allocatedDuringCollection;
    zone->mallocHeapSize.removeBytes(sizeChange, updateRetained);
  }

  uintptr_t startOffset = uintptr_t(alloc) & ChunkMask;
  uintptr_t oldEndOffset = startOffset + currentBytes;
  uintptr_t newEndOffset = startOffset + newBytes;
  MOZ_ASSERT(oldEndOffset <= ChunkSize);

  // Poison freed memory.
  uintptr_t chunkAddr = uintptr_t(chunk);
  PoisonAlloc(reinterpret_cast<void*>(chunkAddr + newEndOffset),
              JS_SWEPT_TENURED_PATTERN, sizeChange,
              MemCheckKind::MakeUndefined);

  // If the chunk is in one of the available lists we may need to move it.
  ChunkLists* availableChunks = getChunkAvailableLists(chunk);
  size_t oldChunkSizeClass = SIZE_MAX;
  if (availableChunks) {
    oldChunkSizeClass = chunk->sizeClassForAvailableLists();
  }

  FreeLists* freeLists = getChunkFreeLists(chunk);
  if (oldEndOffset == ChunkSize || chunk->isAllocated(oldEndOffset)) {
    // If we abut another allocation then add a new free region.
    uintptr_t freeStart = chunkAddr + newEndOffset;
    addFreeRegion(freeLists, freeStart, sizeChange, SizeKind::Medium, false,
                  ListPosition::Front);
  } else {
    // Otherwise find the following free region and extend it down.
    FreeRegion* region =
        chunk->findFollowingFreeRegion(chunkAddr + oldEndOffset);
    MOZ_ASSERT(region->startAddr == chunkAddr + oldEndOffset);
    updateFreeRegionStart(freeLists, region, chunkAddr + newEndOffset,
                          SizeKind::Medium);
  }

  if (availableChunks) {
    maybeUpdateAvailableLists(availableChunks, chunk, oldChunkSizeClass);
  }

  return true;
}

BufferAllocator::FreeLists* BufferAllocator::getChunkFreeLists(
    BufferChunk* chunk) {
  MOZ_ASSERT_IF(majorState == State::Sweeping,
                chunk->allocatedDuringCollection);
  MOZ_ASSERT_IF(
      majorState == State::Marking && !chunk->allocatedDuringCollection,
      chunk->ownsFreeLists);

  if (chunk->ownsFreeLists) {
    // The chunk is in one of the available lists.
    return &chunk->freeLists.ref();
  }

  return &freeLists.ref();
}

BufferAllocator::ChunkLists* BufferAllocator::getChunkAvailableLists(
    BufferChunk* chunk) {
  MOZ_ASSERT_IF(majorState == State::Sweeping,
                chunk->allocatedDuringCollection);

  if (!chunk->ownsFreeLists) {
    return nullptr;  // Chunk is not in either available list.
  }

  if (majorState == State::Marking && !chunk->allocatedDuringCollection) {
    return nullptr;  // Chunk is waiting to be swept.
  }

  if (chunk->hasNurseryOwnedAllocs) {
    return &availableMixedChunks.ref();
  }

  return &availableTenuredChunks.ref();
}

/* static */
size_t BufferAllocator::SizeClassForSmallAlloc(size_t bytes) {
  MOZ_ASSERT(bytes >= MinSmallAllocSize);
  MOZ_ASSERT(bytes <= MaxSmallAllocSize);

  size_t log2Size = mozilla::CeilingLog2(bytes);
  MOZ_ASSERT((size_t(1) << log2Size) >= bytes);
  MOZ_ASSERT(MinSizeClassShift == mozilla::CeilingLog2(MinFreeRegionSize));
  if (log2Size < MinSizeClassShift) {
    return 0;
  }

  size_t sizeClass = log2Size - MinSizeClassShift;
  MOZ_ASSERT(sizeClass <= MaxSmallAllocClass);
  return sizeClass;
}

/* static */
size_t BufferAllocator::SizeClassForMediumAlloc(size_t bytes) {
  MOZ_ASSERT(bytes >= MinMediumAllocSize);
  MOZ_ASSERT(bytes <= MaxMediumAllocSize);

  size_t log2Size = mozilla::CeilingLog2(bytes);
  MOZ_ASSERT((size_t(1) << log2Size) >= bytes);

  MOZ_ASSERT(log2Size >= MinMediumAllocShift);
  size_t sizeClass = log2Size - MinMediumAllocShift + MinMediumAllocClass;

  MOZ_ASSERT(sizeClass >= MinMediumAllocClass);
  MOZ_ASSERT(sizeClass < AllocSizeClasses);
  return sizeClass;
}

/* static */
size_t BufferAllocator::SizeClassForFreeRegion(size_t bytes, SizeKind kind) {
  MOZ_ASSERT(bytes >= MinFreeRegionSize);
  MOZ_ASSERT(bytes < ChunkSize);

  if (kind == SizeKind::Medium && bytes >= MaxMediumAllocSize) {
    // Free regions large enough for MaxMediumAllocSize don't have to have
    // enough space for that size rounded up to the next power of two, as is the
    // case for smaller regions.
    return MaxMediumAllocClass;
  }

  size_t log2Size = mozilla::FloorLog2(bytes);
  MOZ_ASSERT((size_t(1) << log2Size) <= bytes);
  MOZ_ASSERT(log2Size >= MinSizeClassShift);
  size_t sizeClass =
      std::min(log2Size - MinSizeClassShift, AllocSizeClasses - 1);

  if (kind == SizeKind::Small) {
    return std::min(sizeClass, MaxSmallAllocClass);
  }

  sizeClass++;  // Medium size classes start after small ones.

  MOZ_ASSERT(sizeClass >= MinMediumAllocClass);
  MOZ_ASSERT(sizeClass < AllocSizeClasses);
  return sizeClass;
}

/* static */
inline size_t BufferAllocator::SizeClassBytes(size_t sizeClass) {
  MOZ_ASSERT(sizeClass < AllocSizeClasses);

  // The first medium size class is the same size as the last small size class.
  if (sizeClass >= MinMediumAllocClass) {
    sizeClass--;
  }

  return 1 << (sizeClass + MinSizeClassShift);
}

/* static */
bool BufferAllocator::IsMediumAlloc(void* alloc) {
  MOZ_ASSERT(IsBufferAlloc(alloc));

  // Test for large buffers before calling this so we can assume |alloc| is
  // inside a chunk.
  MOZ_ASSERT(!IsLargeAlloc(alloc));

  BufferChunk* chunk = BufferChunk::from(alloc);
  return !chunk->isSmallBufferRegion(alloc);
}

bool BufferAllocator::needLockToAccessBufferMap() const {
  MOZ_ASSERT(CurrentThreadCanAccessZone(zone) || CurrentThreadIsPerformingGC());
  return minorState.refNoCheck() == State::Sweeping ||
         majorState.refNoCheck() == State::Sweeping;
}

LargeBuffer* BufferAllocator::lookupLargeBuffer(void* alloc) {
  MaybeLock lock;
  return lookupLargeBuffer(alloc, lock);
}

LargeBuffer* BufferAllocator::lookupLargeBuffer(void* alloc, MaybeLock& lock) {
  MOZ_ASSERT(lock.isNothing());
  if (needLockToAccessBufferMap()) {
    lock.emplace(this);
  }

  auto ptr = largeAllocMap.ref().readonlyThreadsafeLookup(alloc);
  MOZ_ASSERT(ptr);
  LargeBuffer* buffer = ptr->value();
  MOZ_ASSERT(buffer->data() == alloc);
  MOZ_ASSERT(buffer->zoneFromAnyThread() == zone);
  return buffer;
}

void* BufferAllocator::allocLarge(size_t bytes, bool nurseryOwned, bool inGC) {
  bytes = RoundUp(bytes, ChunkSize);
  MOZ_ASSERT(bytes > MaxMediumAllocSize);
  MOZ_ASSERT(bytes >= bytes);

  // Allocate a small buffer the size of a LargeBuffer to hold the metadata.
  static_assert(sizeof(LargeBuffer) <= MaxSmallAllocSize);
  void* bufferPtr = allocSmall(sizeof(LargeBuffer), nurseryOwned, inGC);
  if (!bufferPtr) {
    return nullptr;
  }

  // Large allocations are aligned to the chunk size, even if they are smaller
  // than a chunk. This allows us to tell large buffer allocations apart by
  // looking at the pointer alignment.
  void* alloc = MapAlignedPages(bytes, ChunkSize, ShouldStallAndRetry(inGC));
  if (!alloc) {
    return nullptr;
  }
  auto freeGuard = mozilla::MakeScopeExit([&]() { UnmapPages(alloc, bytes); });

  CheckHighBitsOfPointer(alloc);

  auto* buffer = new (bufferPtr) LargeBuffer(alloc, bytes, nurseryOwned);

  {
    MaybeLock lock;
    if (needLockToAccessBufferMap()) {
      lock.emplace(this);
    }
    if (!largeAllocMap.ref().putNew(alloc, buffer)) {
      return nullptr;
    }
  }

  freeGuard.release();

  if (nurseryOwned) {
    largeNurseryAllocs.ref().pushBack(buffer);
  } else {
    buffer->allocatedDuringCollection = majorState != State::NotCollecting;
    largeTenuredAllocs.ref().pushBack(buffer);
  }

  // Update memory accounting and trigger an incremental slice if needed.
  if (!nurseryOwned) {
    bool checkThresholds = !inGC;
    updateHeapSize(bytes, checkThresholds, false);
  }

  MOZ_ASSERT(IsLargeAlloc(alloc));
  return alloc;
}

void BufferAllocator::updateHeapSize(size_t bytes, bool checkThresholds,
                                     bool updateRetainedSize) {
  // Update memory accounting and trigger an incremental slice if needed.
  // TODO: This will eventually be attributed to gcHeapSize.
  zone->mallocHeapSize.addBytes(bytes, updateRetainedSize);
  if (checkThresholds) {
    GCRuntime* gc = &zone->runtimeFromAnyThread()->gc;
    gc->maybeTriggerGCAfterMalloc(zone);
  }
}

/* static */
bool BufferAllocator::IsLargeAlloc(void* alloc) {
  return (uintptr_t(alloc) & ChunkMask) == 0;
}

bool BufferAllocator::markLargeTenuredBuffer(LargeBuffer* buffer) {
  MOZ_ASSERT(!buffer->isNurseryOwned);

  if (buffer->allocatedDuringCollection) {
    return false;
  }

  // Bug 1961755: This method can return false positives. A fully atomic version
  // would be preferable in this case.
  auto* region = SmallBufferRegion::from(buffer);
  return region->setMarked(buffer);
}

bool BufferAllocator::isLargeTenuredMarked(LargeBuffer* buffer) {
  MOZ_ASSERT(!buffer->isNurseryOwned);
  MOZ_ASSERT(buffer->zoneFromAnyThread() == zone);
  MOZ_ASSERT(!buffer->isInList());

  auto* region = SmallBufferRegion::from(buffer);
  return region->isMarked(buffer);
}

void BufferAllocator::freeLarge(void* alloc) {
  MaybeLock lock;
  LargeBuffer* buffer = lookupLargeBuffer(alloc, lock);
  MOZ_ASSERT(buffer->zone() == zone);

  DebugOnlyPoison(alloc, JS_FREED_BUFFER_PATTERN, buffer->allocBytes(),
                  MemCheckKind::MakeUndefined);

  if (!buffer->isNurseryOwned && majorState == State::Sweeping &&
      !buffer->allocatedDuringCollection) {
    return;  // Large allocations are currently being swept.
  }

  MOZ_ASSERT(buffer->isInList());

  if (buffer->isNurseryOwned) {
    largeNurseryAllocs.ref().remove(buffer);
  } else if (majorState == State::Marking &&
             !buffer->allocatedDuringCollection) {
    largeTenuredAllocsToSweep.ref().remove(buffer);
  } else {
    largeTenuredAllocs.ref().remove(buffer);
  }

  unmapLarge(buffer, false, lock);
}

bool BufferAllocator::shrinkLarge(LargeBuffer* buffer, size_t newBytes) {
  MOZ_ASSERT(IsLargeAllocSize(newBytes));
#ifdef XP_WIN
  // Can't unmap part of a region mapped with VirtualAlloc on Windows.
  //
  // It is possible to decommit the physical pages so we could do that and
  // track virtual size as well as committed size. This would also allow us to
  // grow the allocation again if necessary.
  return false;
#else
  MOZ_ASSERT(buffer->zone() == zone);

  if (!buffer->isNurseryOwned && majorState == State::Sweeping &&
      !buffer->allocatedDuringCollection) {
    return false;  // Large allocations are currently being swept.
  }

  MOZ_ASSERT(buffer->isInList());

  newBytes = RoundUp(newBytes, ChunkSize);
  size_t oldBytes = buffer->bytes;
  MOZ_ASSERT(oldBytes > newBytes);
  size_t shrinkBytes = oldBytes - newBytes;

  if (!buffer->isNurseryOwned) {
    zone->mallocHeapSize.removeBytes(shrinkBytes, false);
  }

  buffer->bytes = newBytes;

  void* endPtr = reinterpret_cast<void*>(uintptr_t(buffer->data()) + newBytes);
  UnmapPages(endPtr, shrinkBytes);

  return true;
#endif
}

void BufferAllocator::unmapLarge(LargeBuffer* buffer, bool isSweeping,
                                 MaybeLock& lock) {
  unregisterLarge(buffer, isSweeping, lock);
  UnmapPages(buffer->data(), buffer->bytes);
}

void BufferAllocator::unregisterLarge(LargeBuffer* buffer, bool isSweeping,
                                      MaybeLock& lock) {
  MOZ_ASSERT(buffer->zoneFromAnyThread() == zone);
  MOZ_ASSERT(!buffer->isInList());
  MOZ_ASSERT_IF(isSweeping || needLockToAccessBufferMap(), lock.isSome());

#ifdef DEBUG
  auto ptr = largeAllocMap.ref().lookup(buffer->data());
  MOZ_ASSERT(ptr && ptr->value() == buffer);
#endif
  largeAllocMap.ref().remove(buffer->data());

  // Drop the lock now we've updated the map.
  lock.reset();

  if (!buffer->isNurseryOwned) {
    zone->mallocHeapSize.removeBytes(buffer->bytes, isSweeping);
  }
}

#include "js/Printer.h"
#include "util/GetPidProvider.h"

static const char* const BufferAllocatorStatsPrefix = "BufAllc:";

#define FOR_EACH_BUFFER_STATS_FIELD(_)                 \
  _("PID", 7, "%7zu", pid)                             \
  _("Runtime", 14, "0x%12p", runtime)                  \
  _("Timestamp", 10, "%10.6f", timestamp.ToSeconds())  \
  _("Reason", 20, "%-20.20s", reason)                  \
  _("", 2, "%2s", "")                                  \
  _("TotalKB", 8, "%8zu", totalBytes / 1024)           \
  _("UsedKB", 8, "%8zu", stats.usedBytes / 1024)       \
  _("FreeKB", 8, "%8zu", stats.freeBytes / 1024)       \
  _("Zs", 3, "%3zu", zoneCount)                        \
  _("", 7, "%7s", "")                                  \
  _("MixSRs", 6, "%6zu", stats.mixedSmallRegions)      \
  _("TnrSRs", 6, "%6zu", stats.tenuredSmallRegions)    \
  _("MixCs", 6, "%6zu", stats.mixedChunks)             \
  _("TnrCs", 6, "%6zu", stats.tenuredChunks)           \
  _("AMixCs", 6, "%6zu", stats.availableMixedChunks)   \
  _("ATnrCs", 6, "%6zu", stats.availableTenuredChunks) \
  _("FreeRs", 6, "%6zu", stats.freeRegions)            \
  _("LNurAs", 6, "%6zu", stats.largeNurseryAllocs)     \
  _("LTnrAs", 6, "%6zu", stats.largeTenuredAllocs)

/* static */
void BufferAllocator::printStatsHeader(FILE* file) {
  Sprinter sprinter;
  if (!sprinter.init()) {
    return;
  }
  sprinter.put(BufferAllocatorStatsPrefix);

#define PRINT_METADATA_NAME(name, width, _1, _2) \
  sprinter.printf(" %-*s", width, name);

  FOR_EACH_BUFFER_STATS_FIELD(PRINT_METADATA_NAME)
#undef PRINT_METADATA_NAME

  sprinter.put("\n");

  JS::UniqueChars str = sprinter.release();
  if (!str) {
    return;
  }
  fputs(str.get(), file);
}

/* static */
void BufferAllocator::printStats(GCRuntime* gc, mozilla::TimeStamp creationTime,
                                 bool isMajorGC, FILE* file) {
  Sprinter sprinter;
  if (!sprinter.init()) {
    return;
  }
  sprinter.put(BufferAllocatorStatsPrefix);

  size_t pid = getpid();
  JSRuntime* runtime = gc->rt;
  mozilla::TimeDuration timestamp = mozilla::TimeStamp::Now() - creationTime;
  const char* reason = isMajorGC ? "post major slice" : "pre minor GC";

  size_t zoneCount = 0;
  Stats stats;
  for (AllZonesIter zone(gc); !zone.done(); zone.next()) {
    zoneCount++;
    zone->bufferAllocator.getStats(stats);
  }

  size_t totalBytes = stats.usedBytes + stats.freeBytes + stats.adminBytes;

#define PRINT_FIELD_VALUE(_1, _2, format, value) \
  sprinter.printf(" " format, value);

  FOR_EACH_BUFFER_STATS_FIELD(PRINT_FIELD_VALUE)
#undef PRINT_FIELD_VALUE

  sprinter.put("\n");

  JS::UniqueChars str = sprinter.release();
  if (!str) {
    return;
  }

  fputs(str.get(), file);
}

size_t BufferAllocator::getSizeOfNurseryBuffers() {
  maybeMergeSweptData();

  MOZ_ASSERT(minorState == State::NotCollecting);
  MOZ_ASSERT(majorState == State::NotCollecting);

  size_t bytes = 0;

  for (BufferChunk* chunk : mixedChunks.ref()) {
    for (auto alloc = chunk->allocIter(); !alloc.done(); alloc.next()) {
      if (chunk->isNurseryOwned(alloc)) {
        bytes += chunk->allocBytes(alloc);
      }
    }
  }

  for (const LargeBuffer* buffer : largeNurseryAllocs.ref()) {
    bytes += buffer->allocBytes();
  }

  return bytes;
}

void BufferAllocator::addSizeOfExcludingThis(size_t* usedBytesOut,
                                             size_t* freeBytesOut,
                                             size_t* adminBytesOut) {
  maybeMergeSweptData();

  MOZ_ASSERT(minorState == State::NotCollecting);
  MOZ_ASSERT(majorState == State::NotCollecting);

  Stats stats;
  getStats(stats);

  *usedBytesOut += stats.usedBytes;
  *freeBytesOut += stats.freeBytes;
  *adminBytesOut += stats.adminBytes;
}

static void GetChunkStats(BufferChunk* chunk, BufferAllocator::Stats& stats) {
  stats.usedBytes += ChunkSize - FirstMediumAllocOffset;
  stats.adminBytes += FirstMediumAllocOffset;
  for (auto iter = chunk->smallRegionIter(); !iter.done(); iter.next()) {
    SmallBufferRegion* region = iter.get();
    if (region->hasNurseryOwnedAllocs()) {
      stats.mixedSmallRegions++;
    } else {
      stats.tenuredSmallRegions++;
    }
    stats.adminBytes += FirstSmallAllocOffset;
  }
}

void BufferAllocator::getStats(Stats& stats) {
  maybeMergeSweptData();

  MOZ_ASSERT(minorState == State::NotCollecting);

  for (BufferChunk* chunk : mixedChunks.ref()) {
    stats.mixedChunks++;
    GetChunkStats(chunk, stats);
  }
  for (auto chunk = availableMixedChunks.ref().chunkIter(); !chunk.done();
       chunk.next()) {
    stats.availableMixedChunks++;
    GetChunkStats(chunk, stats);
  }
  for (BufferChunk* chunk : tenuredChunks.ref()) {
    stats.tenuredChunks++;
    GetChunkStats(chunk, stats);
  }
  for (auto chunk = availableTenuredChunks.ref().chunkIter(); !chunk.done();
       chunk.next()) {
    stats.availableTenuredChunks++;
    GetChunkStats(chunk, stats);
  }
  for (const LargeBuffer* buffer : largeNurseryAllocs.ref()) {
    stats.largeNurseryAllocs++;
    stats.usedBytes += buffer->allocBytes();
    stats.adminBytes += sizeof(LargeBuffer);
  }
  for (const LargeBuffer* buffer : largeTenuredAllocs.ref()) {
    stats.largeTenuredAllocs++;
    stats.usedBytes += buffer->allocBytes();
    stats.adminBytes += sizeof(LargeBuffer);
  }
  for (auto region = freeLists.ref().freeRegionIter(); !region.done();
       region.next()) {
    stats.freeRegions++;
    size_t size = region->size();
    MOZ_ASSERT(stats.usedBytes >= size);
    stats.usedBytes -= size;
    stats.freeBytes += size;
  }
}
