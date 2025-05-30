/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=2 sw=2 et tw=78:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef DOM_Arena_h___
#define DOM_Arena_h___
#include "nsISupportsImpl.h"
#include "mozmemory.h"

#include "mozilla/mozalloc_oom.h"  // for mozalloc_handle_oom
#include "nsString.h"

#define NS_DECL_DOMARENA_DESTROY void Destroy(void);

#define NS_IMPL_DOMARENA_DESTROY(class)                              \
  void class ::Destroy(void) {                                       \
    if (StaticPrefs::dom_arena_allocator_enabled_AtStartup()) {      \
      RefPtr<nsNodeInfoManager> nim = OwnerDoc()->NodeInfoManager(); \
      RefPtr<DOMArena> arena =                                       \
          HasFlag(NODE_KEEPS_DOMARENA)                               \
              ? nsContentUtils::TakeEntryFromDOMArenaTable(this)     \
              : nullptr;                                             \
      this->~class();                                                \
      MOZ_ASSERT(nim, "nsNodeInfoManager needs to be initialized");  \
      nim->Free(this);                                               \
    } else {                                                         \
      delete this;                                                   \
    }                                                                \
  }

namespace mozilla::dom {

class DOMArena {
 public:
  friend class DocGroup;
  explicit DOMArena(const nsACString& aLabel) {
    nsCString label = PromiseFlatCString("DOMArena "_ns + aLabel);

    arena_params_t params;
    params.mMaxDirtyIncreaseOverride = 7;
    params.mFlags = ARENA_FLAG_THREAD_MAIN_THREAD_ONLY;
    params.mLabel = label.get();
    mArenaId = moz_create_arena_with_params(&params);
  }

  NS_INLINE_DECL_REFCOUNTING(DOMArena)

  void* Allocate(size_t aSize) {
    void* ret = moz_arena_malloc(mArenaId, aSize);
    if (!ret) {
      mozalloc_handle_oom(aSize);
    }
    return ret;
  }

 private:
  ~DOMArena() { moz_dispose_arena(mArenaId); }
  arena_id_t mArenaId;
};
}  // namespace mozilla::dom
#endif
